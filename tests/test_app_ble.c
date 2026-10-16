#include "app/app_ble_protocol.h"
#include "app/app_ble_trust.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_transactions(void) {
    app_ble_rx_t r={0}; const char *json="{\"schema\":1}";
    uint32_t crc=app_ble_crc32(json,strlen(json));
    assert(app_ble_crc32("123456789",9)==0xcbf43926);
    uint8_t begin[]={1,12,0,(uint8_t)crc,(uint8_t)(crc>>8),(uint8_t)(crc>>16),(uint8_t)(crc>>24)};
    uint8_t data[15]={2,0,0};memcpy(data+3,json,12);
    uint8_t commit=3;
    assert(app_ble_rx_feed(&r,data,sizeof(data))==APP_BLE_RX_ERROR);
    assert(app_ble_rx_feed(&r,begin,sizeof(begin))==APP_BLE_RX_MORE);
    assert(app_ble_rx_feed(&r,data,sizeof(data))==APP_BLE_RX_MORE);
    assert(app_ble_rx_feed(&r,&commit,1)==APP_BLE_RX_COMPLETE);
    assert(!strcmp(r.json,json));
    assert(app_ble_rx_feed(&r,&commit,1)==APP_BLE_RX_ERROR); // no replay
    assert(app_ble_rx_feed(&r,begin,sizeof(begin))==APP_BLE_RX_MORE);
    data[1]=1;assert(app_ble_rx_feed(&r,data,sizeof(data))==APP_BLE_RX_ERROR); // gap
    data[1]=0;assert(app_ble_rx_feed(&r,data,sizeof(data))==APP_BLE_RX_ERROR); // restart required
    assert(app_ble_rx_feed(&r,begin,sizeof(begin))==APP_BLE_RX_MORE);
    data[5]^=1;assert(app_ble_rx_feed(&r,data,sizeof(data))==APP_BLE_RX_MORE);
    assert(app_ble_rx_feed(&r,&commit,1)==APP_BLE_RX_ERROR); // CRC
    begin[1]=0;begin[2]=16;assert(app_ble_rx_feed(&r,begin,sizeof(begin))==APP_BLE_RX_ERROR); // 4096
    begin[1]=0;begin[2]=0;assert(app_ble_rx_feed(&r,begin,sizeof(begin))==APP_BLE_RX_ERROR);
    begin[1]=12;assert(app_ble_rx_feed(&r,begin,sizeof(begin))==APP_BLE_RX_MORE);
    data[3]=0;assert(app_ble_rx_feed(&r,data,sizeof(data))==APP_BLE_RX_ERROR); // embedded NUL
}
static void test_identity(void) {
    const char *a="12345678-1234-1234-1234-123456789abc", *b="22345678-1234-1234-1234-123456789abc";
    app_ble_bindings_t bindings={.version=1}; app_ble_peer_t peer={.type=1,.val={1,2,3,4,5,6}};
    assert(app_ble_trust(&bindings,a,&peer,false)==APP_BLE_TRUST_REJECT);
    assert(app_ble_trust(&bindings,a,&peer,true)==APP_BLE_TRUST_CONFIRM);
    strcpy(bindings.hosts[0].id,a);strcpy(bindings.hosts[0].name,"Mac A");bindings.hosts[0].peer=peer;strcpy(bindings.active,a);
    assert(app_ble_trust(&bindings,a,&peer,false)==APP_BLE_TRUST_RESUME);
    app_ble_peer_t attacker=peer;attacker.val[0]^=1;
    assert(app_ble_trust(&bindings,a,&attacker,true)==APP_BLE_TRUST_REJECT); // UUID spoof
    strcpy(bindings.hosts[1].id,b);strcpy(bindings.hosts[1].name,"Mac B");bindings.hosts[1].peer=attacker;
    assert(app_ble_trust(&bindings,b,&attacker,false)==APP_BLE_TRUST_CONFIRM); // every takeover
    assert(app_ble_trust(&bindings,b,&peer,true)==APP_BLE_TRUST_REJECT);
    for(int i=2;i<4;i++) {
        strcpy(bindings.hosts[i].id,a);bindings.hosts[i].id[0]=(char)('0'+i+1);
        strcpy(bindings.hosts[i].name,"Other Mac");bindings.hosts[i].peer=peer;
    }
    assert(app_ble_trust(&bindings,"92345678-1234-1234-1234-123456789abc",&peer,true)==APP_BLE_TRUST_REJECT);
    assert(!app_ble_host_id_valid("AA:BB:CC:DD:EE:FF"));
    memset(bindings.active,'x',sizeof(bindings.active));assert(!app_ble_bindings_valid(&bindings));
}
static void test_audio(const char *fixture) {
    int16_t source[320], decoded[320];uint8_t packet[172];
    for(int i=0;i<320;i++) {int phase=i%160;source[i]=(int16_t)((phase<80?phase:160-phase)*200-8000);}
    app_ble_audio_encode(source,0x12345678,42,packet);
    assert(app_ble_audio_decode(packet,sizeof(packet),decoded));
    long long square_error=0;
    for(int i=0;i<320;i++) {long long delta=source[i]-decoded[i];square_error+=delta*delta;}
    assert(square_error/320<100000);
    assert(packet[0]==1 && packet[1]==0x78 && packet[5]==42);
    if(fixture) {FILE *f=fopen(fixture,"wb");assert(f);assert(fwrite(packet,1,172,f)==172);assert(fwrite(decoded,2,320,f)==320);fclose(f);}
    packet[11]=89;assert(!app_ble_audio_decode(packet,sizeof(packet),decoded));
    assert(!app_ble_audio_decode(packet,171,decoded));
    // Each block is independently decodable after loss, without predictor drift.
    app_ble_audio_encode(source,7,999,packet);assert(app_ble_audio_decode(packet,sizeof(packet),decoded));
    source[0]=INT16_MIN;source[1]=INT16_MAX;app_ble_audio_encode(source,7,1000,packet);
    assert(app_ble_audio_decode(packet,sizeof(packet),decoded));assert(decoded[0]==INT16_MIN);
}
int main(int argc,char **argv) {
    test_transactions();test_identity();test_audio(argc>1?argv[1]:NULL);
    puts("BLE transaction, identity isolation and ADPCM tests: PASS");return 0;
}
