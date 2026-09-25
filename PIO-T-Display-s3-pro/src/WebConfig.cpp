#include "WebConfig.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "Settings.h"
#include "BinanceWebSocket.h"
#include "WiFiProvHelper.h"
#include "IconStore.h"
#include "CoinNames.h"
#include <LittleFS.h>

#define WEB_USER "admin"
#define WEB_REALM "ticker"
#define WEB_PASSWORD_MIN_LEN 6
#define PREFS_NAMESPACE "ticker"
#define PREFS_PASSWORD_KEY "webpass"

static WebServer server(80);
static bool started = false;
static String webPassword; // empty = page is not protected yet

// Digest auth: the password never goes over the network in clear text
static bool checkAuth()
{
    if (webPassword.isEmpty() || server.authenticate(WEB_USER, webPassword.c_str()))
        return true;
    // Sent as text/html without a charset: declare it in the body
    server.requestAuthentication(DIGEST_AUTH, WEB_REALM,
                                 "<!doctype html><meta charset=\"utf-8\"><p>Нужен пароль</p>");
    return false;
}

static String formatNumber(double value)
{
    if (value <= 0)
        return "";
    char buf[24];
    snprintf(buf, sizeof(buf), "%.8g", value);
    return buf;
}

// Accepts "84000.5" and "84000,5"; empty means "off"
static double parseNumber(String text)
{
    text.trim();
    text.replace(',', '.');
    double value = text.toDouble();
    return value > 0 ? value : 0;
}

// SSIDs can contain any characters
static String htmlEscape(const char *text)
{
    String out;
    for (; *text; text++)
    {
        switch (*text)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += *text;
        }
    }
    return out;
}

static void appendWifiSection(String &html)
{
    html += F("<h3>WiFi сети</h3><form method=\"post\" action=\"/wifi\"><datalist id=\"seen\">");
    const char *seen[10];
    int seenCount = wifiScanResults(seen, 10);
    for (int i = 0; i < seenCount; i++)
        html += "<option value=\"" + htmlEscape(seen[i]) + "\">";
    html += F("</datalist>");

    for (int i = 0; i < wifiList.count; i++)
    {
        String idx = String(i);
        html += "<div class=\"row\"><div><input name=\"s" + idx + "\" list=\"seen\" maxlength=\"32\" value=\"" +
                htmlEscape(wifiList.networks[i].ssid) + "\"></div>";
        html += "<div><input name=\"p" + idx + "\" type=\"password\" maxlength=\"64\" placeholder=\"пароль не менять\"></div>";
        html += "<div class=\"narrow\"><input name=\"b" + idx + "\" type=\"number\" min=\"1\" max=\"255\" placeholder=\"100\" title=\"Яркость\" value=\"" +
                (wifiList.networks[i].brightness ? String(wifiList.networks[i].brightness) : String("")) + "\"></div>";
        html += "<label class=\"del\"><input type=\"checkbox\" name=\"d" + idx + "\">удалить</label></div>";
    }
    if (wifiList.count < MAX_WIFI_NETWORKS)
    {
        html += F("<div class=\"row\"><div><input name=\"sn\" list=\"seen\" maxlength=\"32\" placeholder=\"Новая сеть\"></div>"
                  "<div><input name=\"pn\" type=\"password\" maxlength=\"64\" placeholder=\"Пароль\"></div>"
                  "<div class=\"narrow\"><input name=\"bn\" type=\"number\" min=\"1\" max=\"255\" placeholder=\"100\" title=\"Яркость\"></div></div>");
    }

    const char *provisioned = wifiProvisionedSsid();
    if (provisioned[0])
        html += "<small>Ещё сеть из провижининга (приложение ESP SoftAP Prov): " + htmlEscape(provisioned) + ".</small><br>";
    html += F("<small>Третье поле — яркость экрана в этой сети (пусто — 100). "
              "Плата подключается к самой сильной из известных сетей. "
              "Подсказки в поле имени — сети, видимые при последнем сканировании.</small>"
              "<br><button type=\"submit\">Сохранить сети</button></form>");
}

static void appendIconSection(String &html)
{
    html += F("<h3>Иконки пар</h3><table>");
    for (int i = 0; i < settings.tickerCount; i++)
    {
        String name = settings.tickers[i];
        String upper = name;
        upper.toUpperCase();

        html += "<tr><td>" + upper + "</td><td class=\"ico\">";
        if (iconExists(name.c_str()))
            html += "<img src=\"/icon?name=" + name + "\" width=\"26\" height=\"26\">";
        else
            html += "—";
        html += "</td><td><form method=\"post\" enctype=\"multipart/form-data\" action=\"/icon?name=" + name + "\" class=\"up\">"
                "<input type=\"file\" name=\"f\" accept=\"image/png\" required><button type=\"submit\">Загрузить</button></form></td><td>";
        if (iconExists(name.c_str()))
            html += "<form method=\"post\" action=\"/icon/delete?name=" + name + "\"><button type=\"submit\" class=\"sec\">Удалить</button></form>";
        html += "</td></tr>";
    }
    html += F("</table><small>PNG до 16 КБ и до 64×64 px, лучше 32×32 с прозрачным фоном. "
              "Своя иконка заменяет встроенную (BTC, ETH).</small>");
}

static void handleRoot()
{
    if (!checkAuth())
        return;

    String html;
    html.reserve(4096);
    html += F("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>Крипто-тикер</title><style>"
              "body{font-family:system-ui,sans-serif;max-width:560px;margin:auto;padding:16px;background:#111;color:#eee}"
              "h2{color:#ffca41}label{display:block;margin-top:12px}"
              "input{width:100%;box-sizing:border-box;padding:8px;margin:4px 0;background:#222;color:#eee;border:1px solid #444;border-radius:6px;font-size:16px}"
              "table{width:100%;border-collapse:collapse}td,th{padding:4px;text-align:left}"
              ".row{display:flex;gap:8px;align-items:center}.row>div{flex:1}small{color:#999}"
              ".narrow{flex:0 0 64px}.del{display:flex;align-items:center;gap:4px;margin:0;white-space:nowrap}.del input{width:auto}"
              "button{margin-top:16px;padding:12px 24px;background:#ffca41;color:#000;border:0;border-radius:6px;font-weight:bold;font-size:16px}"
              ".warn{background:#5a1d1d;padding:10px;border-radius:6px}"
              ".up{display:flex;gap:6px;align-items:center;margin:0}.up input{font-size:13px;padding:4px}"
              ".up button,button.sec{margin:0;padding:6px 10px;font-size:14px}button.sec{background:#444;color:#eee}"
              ".ico{width:30px;text-align:center}"
              "</style></head><body><h2>Крипто-тикер</h2>");
    if (webPassword.isEmpty())
        html += F("<p class=\"warn\">Страница открыта всем в сети — задайте пароль внизу.</p>");
    html += F("<form method=\"post\" action=\"/save\">");

    html += F("<label>Пары к USDT, через запятую (до 8)</label><input name=\"tickers\" value=\"");
    for (int i = 0; i < settings.tickerCount; i++)
    {
        if (i)
            html += ", ";
        html += settings.tickers[i];
    }
    html += F("\">");

    html += F("<h3>Названия</h3><table>");
    for (int i = 0; i < settings.tickerCount; i++)
    {
        const char *name = settings.tickers[i];
        String upper = name;
        upper.toUpperCase();
        String builtin = coinBuiltinName(name);
        html += "<tr><td>" + upper + "</td><td><input name=\"n_" + String(name) + "\" maxlength=\"24\" placeholder=\"" +
                htmlEscape(builtin.length() ? builtin.c_str() : upper.c_str()) + "\" value=\"" +
                htmlEscape(coinCustomName(name).c_str()) + "\"></td></tr>";
    }
    html += F("</table><small>Пусто — встроенное имя (подсказка в поле). Только латиница: "
              "до 8 символов крупным шрифтом, длиннее — мелким.</small>");

    html += F("<h3>Алерты</h3><table><tr><th>Пара</th><th>Цена</th><th>Выше</th><th>Ниже</th></tr>");
    for (int i = 0; i < settings.tickerCount; i++)
    {
        const char *name = settings.tickers[i];
        double price;
        String upper = name;
        upper.toUpperCase();

        html += "<tr><td>" + upper + "</td><td>";
        html += wsGetPrice(i, &price) ? formatNumber(price) : String("—");
        html += "</td><td><input name=\"a_" + String(name) + "\" inputmode=\"decimal\" value=\"" + formatNumber(settings.alerts[i].above) + "\"></td>";
        html += "<td><input name=\"b_" + String(name) + "\" inputmode=\"decimal\" value=\"" + formatNumber(settings.alerts[i].below) + "\"></td></tr>";
    }
    html += F("</table><small>Срабатывает один раз: экран переключается на пару и мигает ценой. "
              "Кнопка гасит мигание.</small>");

    html += F("<h3>Ночной режим</h3><div class=\"row\">");
    html += "<div><label>С (час)</label><input name=\"ns\" type=\"number\" min=\"0\" max=\"23\" value=\"" + String(settings.nightStartHour) + "\"></div>";
    html += "<div><label>До (час)</label><input name=\"ne\" type=\"number\" min=\"0\" max=\"23\" value=\"" + String(settings.nightEndHour) + "\"></div>";
    html += "<div><label>Яркость</label><input name=\"nb\" type=\"number\" min=\"1\" max=\"255\" value=\"" + String(settings.nightBrightness) + "\"></div>";
    html += F("</div><small>Одинаковые часы — ночной режим выключен.</small>"
              "<br><button type=\"submit\">Сохранить</button></form>");

    appendIconSection(html);
    appendWifiSection(html);

    html += F("<h3>Пароль страницы</h3><form method=\"post\" action=\"/password\"><div class=\"row\">"
              "<div><input name=\"p1\" type=\"password\" autocomplete=\"new-password\" placeholder=\"Новый пароль\"></div>"
              "<div><input name=\"p2\" type=\"password\" autocomplete=\"new-password\" placeholder=\"Ещё раз\"></div></div>"
              "<small>Логин: " WEB_USER ". Не меньше 6 символов. Забыли — удерживайте кнопку 1 пять секунд "
              "(сбросит и пароль, и память WiFi; список сетей на странице останется).</small>"
              "<br><button type=\"submit\">Сменить пароль</button></form></body></html>");

    server.send(200, "text/html; charset=utf-8", html);
}

static void handleSave()
{
    if (!checkAuth())
        return;

    Settings updated = settings;

    // Ticker list: keep existing alerts of pairs that stay in the list
    String list = server.arg("tickers");
    list.replace(' ', ',');
    updated.tickerCount = 0;
    memset(updated.tickers, 0, sizeof(updated.tickers));
    memset(updated.alerts, 0, sizeof(updated.alerts));

    int start = 0;
    while (start <= (int)list.length() && updated.tickerCount < MAX_TICKERS)
    {
        int end = list.indexOf(',', start);
        if (end < 0)
            end = list.length();
        String token = list.substring(start, end);
        start = end + 1;

        char name[TICKER_LEN];
        if (token.length() == 0 || token.length() >= TICKER_LEN || !settingsNormalizeTicker(token.c_str(), name))
            continue;

        bool duplicate = false;
        for (int i = 0; i < updated.tickerCount && !duplicate; i++)
            duplicate = strcmp(updated.tickers[i], name) == 0;
        if (duplicate)
            continue;

        int slot = updated.tickerCount++;
        strlcpy(updated.tickers[slot], name, TICKER_LEN);

        String nameField = "n_" + String(name);
        if (server.hasArg(nameField))
            coinSetCustomName(name, server.arg(nameField));

        // Alerts from the form (only pairs that were on the page have fields)
        String above = "a_" + String(name), below = "b_" + String(name);
        if (server.hasArg(above))
            updated.alerts[slot].above = parseNumber(server.arg(above));
        if (server.hasArg(below))
            updated.alerts[slot].below = parseNumber(server.arg(below));
    }

    if (updated.tickerCount == 0)
    {
        server.send(400, "text/plain; charset=utf-8", "Нужна хотя бы одна пара");
        return;
    }

    updated.nightStartHour = constrain(server.arg("ns").toInt(), 0, 23);
    updated.nightEndHour = constrain(server.arg("ne").toInt(), 0, 23);
    updated.nightBrightness = constrain(server.arg("nb").toInt(), 1, 255);

    // Keep showing the same pair if it is still in the list
    const char *shown = settings.tickers[currentTicker];
    int newCurrent = 0;
    for (int i = 0; i < updated.tickerCount; i++)
    {
        if (strcmp(updated.tickers[i], shown) == 0)
            newCurrent = i;
    }

    settings = updated;
    currentTicker = newCurrent;
    settingsSave();
    setTickerInfo(); // re-publishes streams

    Serial.println("[WEB] Settings saved");
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleWifiSave()
{
    if (!checkAuth())
        return;

    WifiList updated;
    memset(&updated, 0, sizeof(updated));

    auto add = [&](String ssid, const char *pass, long brightness)
    {
        ssid.trim();
        if (ssid.length() == 0 || ssid.length() > 32 || updated.count >= MAX_WIFI_NETWORKS)
            return;
        for (int i = 0; i < updated.count; i++)
        {
            if (ssid == updated.networks[i].ssid)
                return;
        }
        WifiNetwork &net = updated.networks[updated.count++];
        strlcpy(net.ssid, ssid.c_str(), sizeof(net.ssid));
        strlcpy(net.pass, pass, sizeof(net.pass));
        net.brightness = brightness > 0 ? constrain(brightness, 1, 255) : 0;
    };

    for (int i = 0; i < wifiList.count; i++)
    {
        String idx = String(i);
        if (server.hasArg("d" + idx))
            continue;
        String pass = server.arg("p" + idx);
        // Empty password field = keep the stored one
        add(server.arg("s" + idx), pass.length() ? pass.c_str() : wifiList.networks[i].pass, server.arg("b" + idx).toInt());
    }
    String newPass = server.arg("pn");
    add(server.arg("sn"), newPass.c_str(), server.arg("bn").toInt());

    wifiList = updated;
    wifiListSave();
    wifiRetryNow();

    Serial.printf("[WEB] Wi-Fi list saved (%d networks)\n", wifiList.count);
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handlePassword()
{
    if (!checkAuth())
        return;

    String p1 = server.arg("p1"), p2 = server.arg("p2");
    if (p1 != p2 || p1.length() < WEB_PASSWORD_MIN_LEN || p1.length() > 64)
    {
        server.send(400, "text/plain; charset=utf-8", "Пароли не совпадают или короче 6 символов");
        return;
    }

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_PASSWORD_KEY, p1);
    prefs.end();
    webPassword = p1;

    Serial.println("[WEB] Password changed");
    server.sendHeader("Location", "/");
    server.send(303);
}

// Icon upload: the pair comes in the URL (?name=), the file is buffered in
// PSRAM, validated and only then written to LittleFS
static uint8_t *uploadBuf = nullptr;
static size_t uploadLen = 0;
static bool uploadTooBig = false;
static bool uploadAuthorized = false;

static bool validIconName(String &name)
{
    char normalized[TICKER_LEN];
    if (name.length() == 0 || name.length() >= TICKER_LEN || !settingsNormalizeTicker(name.c_str(), normalized))
        return false;
    name = normalized;
    return true;
}

static void refreshIfShown(const String &name)
{
    if (name == settings.tickers[currentTicker])
        setTickerInfo();
}

static void handleIconUploadChunk(HTTPUpload &upload)
{
    if (upload.status == UPLOAD_FILE_START)
    {
        uploadAuthorized = webPassword.isEmpty() || server.authenticate(WEB_USER, webPassword.c_str());
        uploadLen = 0;
        uploadTooBig = false;
        if (!uploadBuf)
            uploadBuf = (uint8_t *)ps_malloc(ICON_MAX_BYTES);
    }
    else if (upload.status == UPLOAD_FILE_WRITE && uploadAuthorized && uploadBuf)
    {
        if (uploadLen + upload.currentSize > ICON_MAX_BYTES)
            uploadTooBig = true;
        else
        {
            memcpy(uploadBuf + uploadLen, upload.buf, upload.currentSize);
            uploadLen += upload.currentSize;
        }
    }
}

static void handleIconUploadDone()
{
    bool hadFile = uploadLen > 0 || uploadTooBig;
    size_t len = uploadLen;
    uploadLen = 0;
    if (!checkAuth())
        return;
    if (!hadFile || !uploadAuthorized)
    {
        server.send(400, "text/plain; charset=utf-8", "Нет файла");
        return;
    }

    String name = server.arg("name");
    if (!validIconName(name))
    {
        server.send(400, "text/plain; charset=utf-8", "Неверное имя пары");
        return;
    }
    const char *error = uploadTooBig ? "Файл больше 16 КБ" : iconValidate(uploadBuf, len);
    if (error)
    {
        server.send(400, "text/plain; charset=utf-8", error);
        return;
    }

    File file = LittleFS.open(iconPath(name.c_str()), "w");
    bool ok = file && file.write(uploadBuf, len) == len;
    file.close();
    if (!ok)
    {
        server.send(500, "text/plain; charset=utf-8", "Не удалось сохранить файл");
        return;
    }

    Serial.printf("[WEB] Icon for %s saved (%u bytes)\n", name.c_str(), len);
    refreshIfShown(name);
    server.sendHeader("Location", "/");
    server.send(303);
}

// server.on(uri, fn, uploadFn) would also call uploadFn for non-multipart
// requests (the body-less first request of digest auth) and crash in
// server.upload(); this handler only takes multipart uploads.
class IconUploadHandler : public RequestHandler
{
public:
    bool canHandle(HTTPMethod method, String uri) override { return method == HTTP_POST && uri == "/icon"; }
    bool canUpload(String uri) override { return uri == "/icon"; }
    bool handle(WebServer &, HTTPMethod, String) override
    {
        handleIconUploadDone();
        return true;
    }
    void upload(WebServer &, String, HTTPUpload &upload) override { handleIconUploadChunk(upload); }
};

static void handleIconDelete()
{
    if (!checkAuth())
        return;
    String name = server.arg("name");
    if (validIconName(name))
    {
        iconDelete(name.c_str());
        refreshIfShown(name);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleIconGet()
{
    if (!checkAuth())
        return;
    String name = server.arg("name");
    if (!validIconName(name) || !iconExists(name.c_str()))
    {
        server.send(404, "text/plain", "Not found");
        return;
    }
    File file = LittleFS.open(iconPath(name.c_str()), "r");
    server.streamFile(file, "image/png");
    file.close();
}

void webConfigResetPassword()
{
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.remove(PREFS_PASSWORD_KEY);
    prefs.end();
    webPassword = "";
}

void webConfigBegin()
{
    if (started)
        return;
    started = true;

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    if (prefs.isKey(PREFS_PASSWORD_KEY))
        webPassword = prefs.getString(PREFS_PASSWORD_KEY);
    prefs.end();

    if (MDNS.begin(WEB_CONFIG_HOSTNAME))
        MDNS.addService("http", "tcp", 80);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/wifi", HTTP_POST, handleWifiSave);
    server.on("/password", HTTP_POST, handlePassword);
    server.on("/icon", HTTP_GET, handleIconGet);
    server.addHandler(new IconUploadHandler());
    server.on("/icon/delete", HTTP_POST, handleIconDelete);
    server.onNotFound([]()
                      { server.send(404, "text/plain", "Not found"); });
    server.begin();

    Serial.printf("[WEB] Settings: http://%s.local or http://%s\n", WEB_CONFIG_HOSTNAME, WiFi.localIP().toString().c_str());
}

void webConfigLoop()
{
    if (started)
        server.handleClient();
}
