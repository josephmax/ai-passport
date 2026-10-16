#include "app_ble_trust.h"
#include "app_ble_protocol.h"
#include <string.h>

bool app_ble_bindings_valid(const app_ble_bindings_t *b) {
    if(b->version!=1 || !memchr(b->active,0,sizeof(b->active))) return false;
    bool active=!b->active[0];
    for(int i=0;i<4;i++) {
        const app_ble_host_t *h=&b->hosts[i];
        if(!memchr(h->id,0,sizeof(h->id)) || !memchr(h->name,0,sizeof(h->name))) return false;
        if(!h->id[0]) continue;
        if(!app_ble_host_id_valid(h->id) || !h->name[0] || h->peer.type>1) return false;
        for(int j=0;j<i;j++) if(!strcmp(h->id,b->hosts[j].id)) return false;
        if(!strcmp(b->active,h->id)) active=true;
    }
    return active;
}
app_ble_trust_t app_ble_trust(const app_ble_bindings_t *b,const char *id,const app_ble_peer_t *peer,bool open) {
    if(!app_ble_bindings_valid(b) || !app_ble_host_id_valid(id)) return APP_BLE_TRUST_REJECT;
    bool room=false;
    for(int i=0;i<4;i++) {
        const app_ble_host_t *h=&b->hosts[i];
        if(!h->id[0]) {room=true;continue;}
        if(strcmp(h->id,id)) continue;
        if(h->peer.type!=peer->type || memcmp(h->peer.val,peer->val,6)) return APP_BLE_TRUST_REJECT;
        return !strcmp(b->active,id)?APP_BLE_TRUST_RESUME:APP_BLE_TRUST_CONFIRM;
    }
    return open && room?APP_BLE_TRUST_CONFIRM:APP_BLE_TRUST_REJECT;
}
