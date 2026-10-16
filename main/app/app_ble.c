#include "app_ble.h"
#include "app_ble_protocol.h"
#include "app_ble_trust.h"
#include "app_ble_startup.h"
#include "app_debug.h"
#include "app_audio_fx.h"
#include "app_runtime.h"
#include "app_snapshot.h"
#include "app_time.h"
#include "bsp_display.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/* UUIDs: 57f1000{0..4}-ac9d-4b41-8c93-20cc0e04d001.
 * 0 service; 1 public identity; 2 authenticated control/status; 3 snapshot;
 * 4 ADPCM notifications. Neither a MAC nor a claimed host UUID authorizes I/O. */
#define UUID(n) BLE_UUID128_INIT(0x01,0xd0,0x04,0x0e,0xcc,0x20,0x93,0x8c,0x41,0x4b,0x9d,0xac,n,0x00,0xf1,0x57)
static const ble_uuid128_t s_service=UUID(0), s_identity=UUID(1), s_control=UUID(2), s_snapshot=UUID(3), s_audio=UUID(4);
static const char *TAG="ble_app";
static uint16_t s_control_handle, s_audio_handle;
static uint8_t s_addr_type;
static portMUX_TYPE s_mux=portMUX_INITIALIZER_UNLOCKED;
static app_ble_status_t s_public;
static app_ble_startup_t s_startup;
static volatile uint32_t s_epoch;
static volatile uint16_t s_conn=BLE_HS_CONN_HANDLE_NONE;
typedef enum { CONNECT, DISCONNECT, NUMERIC, CONTROL, SNAPSHOT, SUBSCRIBE, OPEN, CONFIRM, FORGET, SYNC } command_t;
typedef struct { command_t type; uint32_t epoch, number, confirmation_id; uint16_t conn, size; uint8_t data[244]; } message_t;
typedef app_ble_host_t host_t;
typedef app_ble_bindings_t bindings_t;
typedef struct { char owner[37]; char json[APP_BLE_SNAPSHOT_MAX]; } cache_t;
static bindings_t s_bindings;
static cache_t s_cache;
static app_ble_rx_t s_rx;
static app_snapshot_t s_parsed;
static app_ble_status_t s_state;
static host_t s_candidate;
static nvs_handle_t s_nvs;
static int64_t s_window_until, s_confirm_until, s_link_started;
static int64_t s_sync_deadline;
static uint32_t s_worker_epoch;
static bool s_forget_pending;
static char s_handoff_host[37];
static struct ble_npl_event s_audio_event;
static bool s_audio_pending;
static uint8_t s_audio_packet[APP_BLE_AUDIO_BYTES];
static uint32_t s_audio_epoch;
static uint16_t s_audio_conn;
void ble_store_config_init(void);

app_ble_status_t app_ble_status(void) {
    app_ble_status_t out;
    portENTER_CRITICAL(&s_mux); out=s_public; portEXIT_CRITICAL(&s_mux);
    return out;
}
static void publish(void) {
    s_state.epoch=s_worker_epoch;
    portENTER_CRITICAL(&s_mux); s_public=s_state; portEXIT_CRITICAL(&s_mux);
    app_runtime_publish(APP_EVENT_BLE_STATE);
}
static bool post(command_t type, uint32_t number) {
    message_t m={.type=type,.number=number,.epoch=s_epoch,.conn=s_conn};
    return s_startup.ready && xQueueSend(s_startup.queue,&m,0)==pdTRUE;
}
void app_ble_open_pairing(void) { (void)post(OPEN,0); }
void app_ble_confirm(bool accept,uint32_t displayed_confirmation_id) {
    message_t m={.type=CONFIRM,.number=accept,.epoch=s_epoch,.conn=s_conn,.confirmation_id=displayed_confirmation_id};
    if(s_startup.ready) (void)xQueueSend(s_startup.queue,&m,0);
}
void app_ble_forget(void) { (void)post(FORGET,0); }
void app_ble_request_sync(void) { (void)post(SYNC,0); }
static bool save_bindings(void) {
    return nvs_set_blob(s_nvs,"hosts",&s_bindings,sizeof(s_bindings))==ESP_OK && nvs_commit(s_nvs)==ESP_OK;
}
static host_t *find_host(const char *id) {
    for(int i=0;i<4;i++) if(!strcmp(s_bindings.hosts[i].id,id)) return &s_bindings.hosts[i];
    return NULL;
}
static bool same_peer(const ble_addr_t *a,const app_ble_peer_t *b) { return a->type==b->type && !memcmp(a->val,b->val,6); }
static bool known_peer(const ble_addr_t *peer) {
    for(int i=0;i<4;i++) if(s_bindings.hosts[i].id[0] && same_peer(peer,&s_bindings.hosts[i].peer)) return true;
    return false;
}
static bool secure(uint16_t conn,struct ble_gap_conn_desc *d) {
    return ble_gap_conn_find(conn,d)==0 && d->sec_state.encrypted && d->sec_state.authenticated && d->sec_state.bonded;
}
static void clear_source(void) {
    if(bsp_lvgl_lock(1000)) {
        app_runtime_t *r=app_runtime(); r->snap_valid=false;
        memset(&r->badge,0,sizeof(r->badge)); r->weather_code=0;
        app_runtime_set_snapshot(NULL);
        bsp_lvgl_unlock();
    }
    app_runtime_publish(APP_EVENT_SNAPSHOT_UPDATED);
}
static bool apply_snapshot(bool persist) {
    cJSON *root=cJSON_Parse(s_cache.json);
    cJSON *source=cJSON_GetObjectItemCaseSensitive(root,"source");
    cJSON *host=cJSON_GetObjectItemCaseSensitive(source,"host"), *device=cJSON_GetObjectItemCaseSensitive(source,"device");
    bool scoped=cJSON_IsString(host) && cJSON_IsString(device) && !strcmp(host->valuestring,s_bindings.active) && !strcmp(device->valuestring,s_state.device_id);
    cJSON_Delete(root);
    if(!scoped) return false;
    if(!app_snapshot_parse(s_cache.json,strlen(s_cache.json),&s_parsed)) return false;
    size_t cache_size=sizeof(s_cache.owner)+strlen(s_cache.json)+1;
    if(persist && (nvs_set_blob(s_nvs,"cache",&s_cache,cache_size)!=ESP_OK || nvs_commit(s_nvs)!=ESP_OK)) return false;
    int64_t t=s_parsed.served_at_ms?s_parsed.served_at_ms:s_parsed.generated_at_ms;
    struct timeval tv; gettimeofday(&tv,NULL);
    int64_t now=(int64_t)tv.tv_sec*1000+tv.tv_usec/1000;
    if(persist && t>=(int64_t)APP_TIME_PLAUSIBLE_S*1000 && (t-now>60000 || now-t>60000)) {
        tv.tv_sec=t/1000; tv.tv_usec=(t%1000)*1000; settimeofday(&tv,NULL);
    }
    if(!bsp_lvgl_lock(1000)) return false;
    app_runtime_t *r=app_runtime();
    strlcpy(r->badge.name,s_parsed.badge_name,sizeof(r->badge.name));
    strlcpy(r->badge.role,s_parsed.badge_role,sizeof(r->badge.role));
    app_runtime_set_snapshot(&s_parsed); r->snap_valid=true;
    gettimeofday(&tv,NULL); r->snap_received_at_ms=(int64_t)tv.tv_sec*1000+tv.tv_usec/1000;
    r->weather_code=s_parsed.weather_code;
    if(s_parsed.sunset_min>s_parsed.sunrise_min) {r->sunrise_min=s_parsed.sunrise_min;r->sunset_min=s_parsed.sunset_min;}
    bsp_lvgl_unlock(); app_runtime_publish(APP_EVENT_SNAPSHOT_UPDATED); return true;
}
static void status_json(char *out,size_t cap) {
    app_ble_status_t s=app_ble_status();
    snprintf(out,cap,"{\"v\":1,\"device\":\"%s\",\"ready\":%s,\"mic\":%s,\"sync\":%lu,\"accepted\":%lu,\"error\":\"%s\"}",
        s.device_id,s.ready?"true":"false",app_runtime()->mic_active?"true":"false",
        (unsigned long)s.sync_request,(unsigned long)s.accepted,s.error);
}
static void notify_status(void) {
    if(s_conn==BLE_HS_CONN_HANDLE_NONE) return;
    struct ble_gap_conn_desc d;
    if(!secure(s_conn,&d)) return;
    char text[232]; status_json(text,sizeof(text));
    struct os_mbuf *om=ble_hs_mbuf_from_flat(text,strlen(text));
    if(om) (void)ble_gatts_notify_custom(s_conn,s_control_handle,om);
}
static void audio_on_host(struct ble_npl_event *event) {
    (void)event;
    app_ble_status_t s=app_ble_status();
    struct ble_gap_conn_desc d;
    bool current=s_audio_epoch==s_epoch && s_audio_conn==s_conn;
    bool sending=current && app_runtime()->mic_requested;
    bool ok=false;
    /* GAP callbacks and this event share the host task. Handle reuse cannot
     * interleave between the epoch/security check and notification enqueue. */
    if(sending && s.ready && s.audio_subscribed && s.epoch==s_epoch &&
       secure(s_conn,&d) && ble_att_mtu(s_conn)>=APP_BLE_AUDIO_BYTES+3) {
        struct os_mbuf *om=ble_hs_mbuf_from_flat(s_audio_packet,sizeof(s_audio_packet));
        ok=om && ble_gatts_notify_custom(s_conn,s_audio_handle,om)==0;
    }
    portENTER_CRITICAL(&s_mux);s_audio_pending=false;portEXIT_CRITICAL(&s_mux);
    if(!ok && sending) {
        strlcpy(app_runtime()->mic_error,"BLE audio interrupted",sizeof(app_runtime()->mic_error));
        app_audio_microphone(false);
    }
}
bool app_ble_audio_send(const uint8_t *data,size_t size) {
    app_ble_status_t s=app_ble_status();
    if(size!=APP_BLE_AUDIO_BYTES || !s.ready || !s.audio_subscribed || s.epoch!=s_epoch) return false;
    portENTER_CRITICAL(&s_mux);
    if(s_audio_pending) {portEXIT_CRITICAL(&s_mux);return false;}
    s_audio_pending=true;s_audio_epoch=s.epoch;s_audio_conn=s_conn;
    memcpy(s_audio_packet,data,size);
    portEXIT_CRITICAL(&s_mux);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&s_audio_event);
    return true;
}
static int access_cb(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg) {
    (void)attr; intptr_t kind=(intptr_t)arg;
    if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        char text[232];
        if(kind==1) snprintf(text,sizeof(text),"{\"v\":1,\"device\":\"%s\",\"codec\":\"ima320\",\"rate\":16000}",s_state.device_id);
        else status_json(text,sizeof(text));
        return os_mbuf_append(ctxt->om,text,strlen(text))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    struct ble_gap_conn_desc d;
    if(!secure(conn,&d)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    message_t m={.type=kind==2?CONTROL:SNAPSHOT,.epoch=s_epoch,.conn=conn};
    m.size=OS_MBUF_PKTLEN(ctxt->om);
    if(!m.size || m.size>sizeof(m.data)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if(ble_hs_mbuf_to_flat(ctxt->om,m.data,sizeof(m.data),NULL)) return BLE_ATT_ERR_UNLIKELY;
    return xQueueSend(s_startup.queue,&m,0)==pdTRUE?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def s_services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&s_service.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&s_identity.u,.access_cb=access_cb,.arg=(void *)1,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&s_control.u,.access_cb=access_cb,.arg=(void *)2,.val_handle=&s_control_handle,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&s_snapshot.u,.access_cb=access_cb,.arg=(void *)3,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&s_audio.u,.access_cb=access_cb,.arg=(void *)4,.val_handle=&s_audio_handle,.flags=BLE_GATT_CHR_F_NOTIFY},
        {0}}}, {0}};
static void advertise(void);
static int gap_cb(struct ble_gap_event *e,void *arg) {
    (void)arg; message_t m={.epoch=s_epoch,.conn=s_conn};
    switch(e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if(e->connect.status) {advertise();break;}
        s_conn=e->connect.conn_handle; ++s_epoch;
        m=(message_t){.type=CONNECT,.epoch=s_epoch,.conn=s_conn};
        if(xQueueSend(s_startup.queue,&m,0)!=pdTRUE) ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn=BLE_HS_CONN_HANDLE_NONE; ++s_epoch;
        (void)post(DISCONNECT,0); app_audio_microphone(false); advertise(); break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if(e->passkey.params.action==BLE_SM_IOACT_NUMCMP) {
            m.type=NUMERIC; m.number=e->passkey.params.numcmp;
            if(xQueueSend(s_startup.queue,&m,0)!=pdTRUE) ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
        } else ble_gap_terminate(s_conn,BLE_ERR_AUTH_FAIL);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* A stale OS bond must be explicitly removed, never silently replaced. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if(e->subscribe.attr_handle==s_audio_handle) {m.type=SUBSCRIBE;m.number=e->subscribe.cur_notify;(void)xQueueSend(s_startup.queue,&m,0);}
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if(e->enc_change.status) ble_gap_terminate(s_conn,BLE_ERR_AUTH_FAIL);
        break;
    default: break;
    }
    return 0;
}
static void advertise(void) {
    struct ble_hs_adv_fields f={0}; f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t *)&s_service; f.num_uuids128=1; f.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&f);
    if(rc) {ESP_LOGE(TAG,"advertising fields failed: %d",rc);return;}
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
    rc=ble_gap_adv_start(s_addr_type,NULL,BLE_HS_FOREVER,&p,gap_cb,NULL);
    if(rc) ESP_LOGE(TAG,"advertising start failed: %d",rc);
    else ESP_LOGI(TAG,"advertising ready (device %s)",s_state.device_id);
}
static void on_sync(void) {
    int rc=ble_hs_id_infer_auto(0,&s_addr_type);
    if(rc) ESP_LOGE(TAG,"identity initialization failed: %d",rc);
    else advertise();
}
static void on_reset(int reason) {
    (void)reason;s_conn=BLE_HS_CONN_HANDLE_NONE;++s_epoch;
    app_audio_microphone(false);(void)post(DISCONNECT,0);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); }
static void error(const char *text) {strlcpy(s_state.error,text,sizeof(s_state.error));s_rx.receiving=false;}
static void ready(void) {
    s_state.confirm=false; s_state.numeric=false; s_state.ready=true;
    s_state.error[0]=0; strlcpy(s_state.host_name,s_candidate.name,sizeof(s_state.host_name));
    app_runtime()->paired=true;
}
static void hello(const message_t *m) {
    struct ble_gap_conn_desc d;
    if(!secure(m->conn,&d)) {error("authentication required");return;}
    cJSON *root=cJSON_ParseWithLength((const char *)m->data,m->size);
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,"v");
    const cJSON *op=cJSON_GetObjectItemCaseSensitive(root,"op");
    if(cJSON_IsNumber(v) && v->valuedouble==1 && cJSON_IsString(op) && !strcmp(op->valuestring,"stop") && s_state.ready) {
        app_audio_microphone(false); cJSON_Delete(root); return;
    }
    if(cJSON_IsNumber(v) && v->valuedouble==1 && cJSON_IsString(op) && !strcmp(op->valuestring,"revoke") && s_state.ready) {
        bindings_t previous=s_bindings;
        host_t *slot=find_host(s_bindings.active);
        if(slot) memset(slot,0,sizeof(*slot));
        s_bindings.active[0]=0;
        if(!save_bindings()) {s_bindings=previous;error("binding storage failed");cJSON_Delete(root);return;}
        app_audio_microphone(false);s_state.ready=false;s_state.confirm=false;app_runtime()->paired=false;clear_source();
        if(!known_peer(&d.peer_id_addr)) (void)ble_store_util_delete_peer(&d.peer_id_addr);
        cJSON_Delete(root);ble_gap_terminate(m->conn,BLE_ERR_REM_USER_CONN_TERM);return;
    }
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"host");
    const cJSON *name=cJSON_GetObjectItemCaseSensitive(root,"name");
    if(!cJSON_IsNumber(v) || v->valuedouble!=1 || !cJSON_IsString(id) || !app_ble_host_id_valid(id->valuestring) ||
       !cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring)>32) {error("invalid hello");cJSON_Delete(root);return;}
    if(s_state.pairing_open && s_handoff_host[0] && !strcmp(id->valuestring,s_handoff_host)) {
        error("switch window; retry after 60s");cJSON_Delete(root);
        ble_gap_terminate(m->conn,BLE_ERR_REM_USER_CONN_TERM);return;
    }
    for(const char *p=name->valuestring;*p;p++) if(*p<32 || *p>126) {error("use ASCII host name");cJSON_Delete(root);return;}
    app_ble_peer_t peer={.type=d.peer_id_addr.type};memcpy(peer.val,d.peer_id_addr.val,6);
    app_ble_trust_t trust=app_ble_trust(&s_bindings,id->valuestring,&peer,s_state.pairing_open);
    if(trust==APP_BLE_TRUST_REJECT) {error("host conflict, full or pairing closed");cJSON_Delete(root);return;}
    memset(&s_candidate,0,sizeof(s_candidate)); s_candidate.peer=peer;
    strlcpy(s_candidate.id,id->valuestring,sizeof(s_candidate.id)); strlcpy(s_candidate.name,name->valuestring,sizeof(s_candidate.name));
    cJSON_Delete(root);
    if(trust==APP_BLE_TRUST_RESUME) {ready();return;}
    if(app_runtime()->mic_requested) {error("stop microphone first");return;}
    s_state.ready=false;s_state.confirm=true;s_state.numeric=false;s_confirm_until=esp_timer_get_time()+60000000;
    ++s_state.confirmation_id;
    snprintf(s_state.prompt,sizeof(s_state.prompt),"Use data from\n%s?\nOK accept / hold OK reject",s_candidate.name);
}
static void confirm_host(void) {
    host_t *slot=find_host(s_candidate.id);
    if(!slot) for(int i=0;i<4;i++) if(!s_bindings.hosts[i].id[0]) {slot=&s_bindings.hosts[i];break;}
    if(!slot) {error("4 hosts full; forget first");return;}
    bindings_t previous=s_bindings; *slot=s_candidate;
    bool changed=strcmp(s_bindings.active,s_candidate.id)!=0;
    strlcpy(s_bindings.active,s_candidate.id,sizeof(s_bindings.active));
    if(!save_bindings()) {s_bindings=previous;error("binding storage failed");return;}
    if(changed) clear_source();
    s_window_until=0;s_state.pairing_open=false;s_handoff_host[0]=0; ready();
}
static void worker(void *arg) {
    (void)arg; message_t m;
    for(;;) {
        bool got=xQueueReceive(s_startup.queue,&m,pdMS_TO_TICKS(250))==pdTRUE;
        int64_t now=esp_timer_get_time();
        if(s_state.pairing_open && now>s_window_until) s_state.pairing_open=false;
        if(s_sync_deadline && now>s_sync_deadline) {
            s_sync_deadline=0;app_runtime()->sync_result_valid=true;app_runtime()->sync_last_ok=false;error("sync timed out");
        }
        if(s_state.confirm && now>s_confirm_until) {
            s_state.confirm=false; s_state.ready=false; s_forget_pending=false; error("confirmation timed out");
            if(s_conn!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
        }
        if(got && (m.type>=OPEN || m.epoch==s_epoch)) {
            switch(m.type) {
            case OPEN:
                if(app_runtime()->mic_requested) {error("stop microphone first");break;}
                s_window_until=now+60000000;s_state.pairing_open=true;s_state.error[0]=0;
                strlcpy(s_handoff_host,s_state.ready && s_state.connected?s_bindings.active:"",sizeof(s_handoff_host));
                if(s_conn!=BLE_HS_CONN_HANDLE_NONE) {
                    s_state.ready=false;ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
                }
                break;
            case CONNECT: {
                s_worker_epoch=m.epoch;s_link_started=now;s_state.connected=true;s_state.ready=false;
                s_state.audio_subscribed=false;s_state.confirm=false;s_forget_pending=false;s_rx.receiving=false;
                struct ble_gap_conn_desc d;
                if(ble_gap_conn_find(m.conn,&d) || (!known_peer(&d.peer_id_addr) && !s_state.pairing_open)) ble_gap_terminate(m.conn,BLE_ERR_REM_USER_CONN_TERM);
                break;
            }
            case DISCONNECT: s_state.connected=false;s_state.ready=false;s_state.confirm=false;s_forget_pending=false;s_state.audio_subscribed=false;s_rx.receiving=false;break;
            case NUMERIC:
                if(!s_state.pairing_open) {ble_gap_terminate(m.conn,BLE_ERR_AUTH_FAIL);break;}
                s_state.numeric=true;s_state.confirm=true;s_state.number=m.number;s_confirm_until=now+30000000;
                s_forget_pending=false;
                ++s_state.confirmation_id;
                snprintf(s_state.prompt,sizeof(s_state.prompt),"Compare on Mac\n%06lu\nOK match / hold OK reject",(unsigned long)m.number);break;
            case SUBSCRIBE: s_state.audio_subscribed=m.number!=0;break;
            case CONTROL:
                if(s_forget_pending && s_state.confirm) error("local confirmation pending");
                else hello(&m);
                break;
            case CONFIRM:
                if(!s_state.confirm || m.confirmation_id!=s_state.confirmation_id ||
                   (!s_forget_pending && m.epoch!=s_epoch)) break;
                s_state.confirm=false;
                if(s_forget_pending) {
                    s_forget_pending=false;
                    if(m.number) {
                        bindings_t previous=s_bindings;memset(&s_bindings,0,sizeof(s_bindings));s_bindings.version=1;
                        if(save_bindings()) {clear_source(); (void)ble_store_clear();app_runtime()->paired=false;s_state.ready=false;}
                        else {s_bindings=previous;error("binding storage failed");}
                        if(s_conn!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
                    }
                } else if(s_state.numeric) {
                    struct ble_sm_io io={.action=BLE_SM_IOACT_NUMCMP,.numcmp_accept=m.number!=0};
                    (void)ble_sm_inject_io(s_conn,&io);s_state.numeric=false;
                } else if(m.number && m.epoch==s_worker_epoch && m.epoch==s_epoch) confirm_host();
                else if(s_conn!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
                break;
            case FORGET:
                if(app_runtime()->mic_requested) {error("stop microphone first");break;}
                s_forget_pending=true;s_state.confirm=true;s_state.numeric=false;s_confirm_until=now+30000000;
                ++s_state.confirmation_id;
                strlcpy(s_state.prompt,"Forget all computers?\nOK accept / hold OK reject",sizeof(s_state.prompt));break;
            case SYNC:
                if(s_state.ready) {++s_state.sync_request;s_sync_deadline=now+30000000;s_state.error[0]=0;}
                break;
            case SNAPSHOT:
                if(!s_state.ready || app_runtime()->mic_requested) {error("not ready or microphone busy");break;}
                switch(app_ble_rx_feed(&s_rx,m.data,m.size)) {
                case APP_BLE_RX_ERROR: error("snapshot framing or CRC");break;
                case APP_BLE_RX_COMPLETE:
                    strlcpy(s_cache.owner,s_bindings.active,sizeof(s_cache.owner));memcpy(s_cache.json,s_rx.json,s_rx.used+1);
                    if(apply_snapshot(true)) {++s_state.accepted;s_state.error[0]=0;app_runtime()->sync_last_ok=true;s_sync_deadline=0;}
                    else {error("snapshot invalid or storage failed");app_runtime()->sync_last_ok=false;}
                    app_runtime()->sync_result_valid=true;break;
                default: break;
                }
                break;
            }
        }
        if(s_state.connected && !s_state.ready && !s_state.confirm && now-s_link_started>90000000 && s_conn!=BLE_HS_CONN_HANDLE_NONE)
            ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
        publish(); notify_status();
    }
}
static int configure_services(void) {
    ble_npl_event_init(&s_audio_event,audio_on_host,NULL);
    ble_svc_gap_init();ble_svc_gatt_init();
    int rc=ble_svc_gap_device_name_set("AI Passport");
    if(rc) return rc;
    ble_hs_cfg.sync_cb=on_sync;ble_hs_cfg.reset_cb=on_reset;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    rc=ble_gatts_count_cfg(s_services);
    if(!rc) rc=ble_gatts_add_svcs(s_services);
    if(!rc) rc=ble_att_set_preferred_mtu(247);
    return rc;
}

bool app_ble_init(void) {
    if(s_startup.ready) return true;
    uint8_t mac[6]; if(esp_read_mac(mac,ESP_MAC_EFUSE_FACTORY)!=ESP_OK) return false;
    snprintf(s_state.device_id,sizeof(s_state.device_id),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    if(nvs_open("ble_app",NVS_READWRITE,&s_nvs)!=ESP_OK) return false;
    size_t size=sizeof(s_bindings);
    if(nvs_get_blob(s_nvs,"hosts",&s_bindings,&size)!=ESP_OK || size!=sizeof(s_bindings) || !app_ble_bindings_valid(&s_bindings)) {
        memset(&s_bindings,0,sizeof(s_bindings));s_bindings.version=1;
    }
    /* Old Wi-Fi snapshots have no owner identity; never attribute them to BLE. */
    clear_source(); app_runtime()->paired=s_bindings.active[0]!=0;
    size=sizeof(s_cache);
    if(nvs_get_blob(s_nvs,"cache",&s_cache,&size)==ESP_OK && size>sizeof(s_cache.owner) && size<=sizeof(s_cache) &&
       memchr(s_cache.owner,0,sizeof(s_cache.owner)) && memchr(s_cache.json,0,size-sizeof(s_cache.owner)) &&
       s_bindings.active[0] && !strcmp(s_cache.owner,s_bindings.active)) (void)apply_snapshot(false);
    const app_ble_startup_config_t config={
        .message_size=sizeof(message_t),.worker=worker,.host=host_task,
        .configure=configure_services,
    };
    if(!app_ble_startup(&s_startup,&config)) {
        nvs_close(s_nvs);s_nvs=0;
        snprintf(s_state.error,sizeof(s_state.error),"BLE init: %s",
                 s_startup.failed_stage?s_startup.failed_stage:"cleanup failed");
        publish();
        return false;
    }
    app_debug_register("ble_app",s_startup.worker);
    app_debug_register("nimble_host",s_startup.host);
    publish();
    return true;
}
