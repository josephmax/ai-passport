#include "app_ble_protocol.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p+2)<<16); }
static void put32(uint8_t *p, uint32_t n) { for (int i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
uint32_t app_ble_crc32(const void *data, size_t size) {
    const uint8_t *p=data; uint32_t c=UINT32_MAX;
    while(size--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1)^((c&1)?0xedb88320U:0); }
    return ~c;
}
app_ble_rx_result_t app_ble_rx_feed(app_ble_rx_t *r,const uint8_t *p,size_t n) {
    if(!r || !p || !n) return APP_BLE_RX_ERROR;
    if(p[0]==1 && n==7) {
        memset(r,0,sizeof(*r)); r->expected=u16(p+1); r->crc=u32(p+3);
        r->receiving=r->expected>0 && r->expected<APP_BLE_SNAPSHOT_MAX;
        return r->receiving?APP_BLE_RX_MORE:APP_BLE_RX_ERROR;
    }
    if(p[0]==2 && n>3 && r->receiving && u16(p+1)==r->used &&
       n-3<=(size_t)(r->expected-r->used) && !memchr(p+3,0,n-3)) {
        memcpy(r->json+r->used,p+3,n-3); r->used+=(uint16_t)(n-3); return APP_BLE_RX_MORE;
    }
    if(p[0]==3 && n==1 && r->receiving && r->used==r->expected &&
       app_ble_crc32(r->json,r->used)==r->crc) {
        r->json[r->used]=0; r->receiving=false; return APP_BLE_RX_COMPLETE;
    }
    r->receiving=false; return APP_BLE_RX_ERROR;
}
bool app_ble_host_id_valid(const char *s) {
    if(!s || strlen(s)!=36) return false;
    for(int i=0;i<36;i++) {
        if(i==8 || i==13 || i==18 || i==23) { if(s[i]!='-') return false; }
        else if(!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='f'))) return false;
    }
    return true;
}
static const int steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int indices[8]={-1,-1,-1,-1,2,4,6,8};
static int apply(int nib,int *predictor,int *index) {
    int step=steps[*index], diff=step/8;
    if(nib&1) diff+=step/4;
    if(nib&2) diff+=step/2;
    if(nib&4) diff+=step;
    int p=*predictor+((nib&8)?-diff:diff);
    *predictor=p>32767?32767:p< -32768?-32768:p;
    int i=*index+indices[nib&7]; *index=i>88?88:i<0?0:i;
    return *predictor;
}
void app_ble_audio_encode(const int16_t pcm[APP_BLE_AUDIO_SAMPLES],uint32_t stream,uint32_t seq,uint8_t out[APP_BLE_AUDIO_BYTES]) {
    memset(out,0,APP_BLE_AUDIO_BYTES); out[0]=1; put32(out+1,stream); put32(out+5,seq);
    int pred=pcm[0], index=40; out[9]=(uint8_t)pred; out[10]=(uint8_t)(pred>>8); out[11]=(uint8_t)index;
    for(int i=1;i<APP_BLE_AUDIO_SAMPLES;i++) {
        int d=(int)pcm[i]-pred, nib=d<0?8:0, step=steps[index]; if(d<0) d=-d;
        if(d>=step) {nib|=4;d-=step;} if(d>=step/2) {nib|=2;d-=step/2;} if(d>=step/4) nib|=1;
        apply(nib,&pred,&index); int j=i-1; out[12+j/2]|=(uint8_t)(nib<<((j%2)*4));
    }
}
bool app_ble_audio_decode(const uint8_t *p,size_t n,int16_t pcm[APP_BLE_AUDIO_SAMPLES]) {
    if(n!=APP_BLE_AUDIO_BYTES || p[0]!=1 || p[11]>88) return false;
    int pred=(int16_t)u16(p+9), index=p[11]; pcm[0]=(int16_t)pred;
    for(int i=1;i<APP_BLE_AUDIO_SAMPLES;i++) {int j=i-1;pcm[i]=(int16_t)apply((p[12+j/2]>>((j%2)*4))&15,&pred,&index);}
    return true;
}
