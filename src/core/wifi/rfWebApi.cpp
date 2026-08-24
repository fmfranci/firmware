#include "rfWebApi.h"

#include "core/configPins.h"
#include "core/wifi/webInterface.h"
#include "modules/rf/rf_utils.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <globals.h>

// Defined in webInterface.cpp. Reuse Bruce's existing WebUI session authentication.
extern bool checkUserWebAuth(AsyncWebServerRequest *request, bool onFailureReturnLoginPage);

namespace {
bool rfWebRxActive = false;
float rfWebFrequency = 433.92f;
unsigned long rfWebStartedAt = 0;

bool validFrequency(float mhz) {
    return (mhz >= 280.0f && mhz <= 350.0f) || (mhz >= 387.0f && mhz <= 468.0f) ||
           (mhz >= 779.0f && mhz <= 928.0f);
}

bool requireAuth(AsyncWebServerRequest *request) { return checkUserWebAuth(request, false); }

void sendJson(AsyncWebServerRequest *request, int code, const String &json) {
    AsyncWebServerResponse *response = request->beginResponse(code, "application/json", json);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

String statusJson() {
    const bool cc1101Selected = bruceConfigPins.rfModule == CC1101_SPI_MODULE;
    bool connected = false;
    int rssi = -127;

    if (cc1101Selected) {
        connected = ELECHOUSE_cc1101.getCC1101();
        if (rfWebRxActive && connected) rssi = ELECHOUSE_cc1101.getRssi();
    }

    String json = "{";
    json += "\"cc1101Selected\":" + String(cc1101Selected ? "true" : "false");
    json += ",\"connected\":" + String(connected ? "true" : "false");
    json += ",\"rxActive\":" + String(rfWebRxActive ? "true" : "false");
    json += ",\"frequencyMHz\":" + String(rfWebFrequency, 3);
    json += ",\"rssi\":" + String(rssi);
    json += ",\"uptimeMs\":" + String(rfWebRxActive ? millis() - rfWebStartedAt : 0);
    json += "}";
    return json;
}

bool readFrequency(AsyncWebServerRequest *request, float &frequency) {
    const AsyncWebParameter *param = nullptr;
    if (request->hasParam("frequency", true)) param = request->getParam("frequency", true);
    else if (request->hasParam("frequency")) param = request->getParam("frequency");
    if (!param) return false;

    frequency = param->value().toFloat();
    return validFrequency(frequency);
}

const char RF_REMOTE_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Bruce CC1101 RX</title><style>
:root{color-scheme:dark}body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#0b0d10;color:#f5f7fa;margin:0;padding:20px}main{max-width:620px;margin:auto}.card{background:#15191f;border:1px solid #2b313b;border-radius:18px;padding:18px;margin:12px 0}.big{font-size:2.2rem;font-weight:700}.muted{color:#9aa4b2}.row{display:flex;gap:10px;flex-wrap:wrap}button,input{font:inherit;border-radius:12px;border:1px solid #3a4350;padding:13px}button{background:#f5f7fa;color:#111;font-weight:650;flex:1}button.stop{background:#2a3038;color:#fff}input{background:#0d1014;color:#fff;width:150px}.ok{color:#7ee787}.off{color:#ff7b72}code{font-size:.9rem}</style></head><body><main>
<h1>CC1101 · RX</h1><div class="card"><div id="state" class="big">Carregando…</div><div id="freq" class="muted">—</div><div id="rssi" class="big">— dBm</div><div id="hw" class="muted"></div></div>
<div class="card"><div class="row"><input id="frequency" type="number" min="280" max="928" step="0.001" value="433.920"><button onclick="startRx()">Iniciar RX</button><button class="stop" onclick="stopRx()">Parar RX</button></div><p class="muted">Somente recepção. Faixas aceitas pelo driver Bruce: 280–350, 387–468 e 779–928 MHz.</p></div>
<div class="card muted">Interface local do Bruce para monitoramento do CC1101. Esta versão não possui transmissão, replay, brute force ou jammer.</div>
<script>
async function call(path,opts){const r=await fetch(path,opts);if(!r.ok)throw new Error(await r.text());return r.json()}
async function refresh(){try{const s=await call('/api/rf/status');state.textContent=s.rxActive?'RX ATIVO':'RX PARADO';state.className='big '+(s.rxActive?'ok':'off');freq.textContent=s.frequencyMHz.toFixed(3)+' MHz';rssi.textContent=s.rxActive?s.rssi+' dBm':'— dBm';hw.textContent=s.cc1101Selected?(s.connected?'CC1101 conectado':'CC1101 não respondeu'):'CC1101 não selecionado no Bruce';}catch(e){state.textContent='Sem conexão';state.className='big off'}}
async function startRx(){const f=frequency.value;await call('/api/rf/start?frequency='+encodeURIComponent(f),{method:'POST'});refresh()}
async function stopRx(){await call('/api/rf/stop',{method:'POST'});refresh()}
setInterval(refresh,750);refresh();
</script></main></body></html>
)HTML";
} // namespace

void configureRfWebApi() {
    if (!server) return;

    server->on("/rf-remote", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        request->send(200, "text/html; charset=utf-8", RF_REMOTE_HTML);
    });

    server->on("/api/rf/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        sendJson(request, 200, statusJson());
    });

    server->on("/api/rf/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
            sendJson(request, 409, "{\"error\":\"CC1101 is not the selected RF module\"}");
            return;
        }

        float frequency = rfWebFrequency;
        if (request->hasParam("frequency") || request->hasParam("frequency", true)) {
            if (!readFrequency(request, frequency)) {
                sendJson(request, 400, "{\"error\":\"Invalid or unsupported frequency\"}");
                return;
            }
        }

        if (rfWebRxActive) deinitRfModule();
        if (!initRfModule("rx", frequency)) {
            rfWebRxActive = false;
            sendJson(request, 503, "{\"error\":\"CC1101 initialization failed\"}");
            return;
        }

        rfWebFrequency = frequency;
        rfWebRxActive = true;
        rfWebStartedAt = millis();
        sendJson(request, 200, statusJson());
    });

    server->on("/api/rf/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        if (rfWebRxActive) deinitRfModule();
        rfWebRxActive = false;
        sendJson(request, 200, statusJson());
    });

    server->on("/api/rf/frequency", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!requireAuth(request)) return;
        float frequency = 0;
        if (!readFrequency(request, frequency)) {
            sendJson(request, 400, "{\"error\":\"Invalid or unsupported frequency\"}");
            return;
        }
        rfWebFrequency = frequency;
        if (rfWebRxActive) {
            deinitRfModule();
            if (!initRfModule("rx", frequency)) {
                rfWebRxActive = false;
                sendJson(request, 503, "{\"error\":\"CC1101 reinitialization failed\"}");
                return;
            }
            rfWebStartedAt = millis();
        }
        sendJson(request, 200, statusJson());
    });
}
