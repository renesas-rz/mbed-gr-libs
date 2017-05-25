/* ESP32 Example
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ESP32.h"

ESP32::ESP32(PinName tx, PinName rx, bool debug)
    : _serial(tx, rx, 1024), _parser(_serial)
    , _packets(0), _packets_end(&_packets)
    , _id_bits(0), _server_act(false)
{
    _serial.baud(115200);
    _parser.debugOn(debug);
}

bool ESP32::startup(int mode)
{
    //only 3 valid modes
    if (mode < 1 || mode > 3) {
        return false;
    }
    _lock.lock();
    _parser.setTimeout(1500);
    _parser.recv("ready");
    reset();

    _parser.setTimeout(5000);
    bool success = _parser.send("AT+CWMODE=%d", mode)
                && _parser.recv("OK")
                && _parser.send("AT+CIPMUX=1")
                && _parser.recv("OK");

    _parser.oob("+IPD", this, &ESP32::_packet_handler);

    _parser.oob("0,CONNECT", this, &ESP32::_connect_handler_0);
    _parser.oob("1,CONNECT", this, &ESP32::_connect_handler_1);
    _parser.oob("2,CONNECT", this, &ESP32::_connect_handler_2);
    _parser.oob("3,CONNECT", this, &ESP32::_connect_handler_3);
    _parser.oob("4,CONNECT", this, &ESP32::_connect_handler_4);

    _parser.oob("0,CLOSED", this, &ESP32::_closed_handler_0);
    _parser.oob("1,CLOSED", this, &ESP32::_closed_handler_1);
    _parser.oob("2,CLOSED", this, &ESP32::_closed_handler_2);
    _parser.oob("3,CLOSED", this, &ESP32::_closed_handler_3);
    _parser.oob("4,CLOSED", this, &ESP32::_closed_handler_4);
    _lock.unlock();

    return success;
}

bool ESP32::startup_retry(int mode)
{
    bool success;

    //only 3 valid modes
    if (mode < 1 || mode > 3) {
        return false;
    }

    int wk_mode;

    _lock.lock();
    reset();
    _parser.setTimeout(5000);
    success = _parser.send("AT+CWMODE?")
           && _parser.recv("+CWMODE:%d", &wk_mode)
           && _parser.recv("OK");
    if (success && (wk_mode != mode)) {
        success = _parser.send("AT+CWMODE=%d", mode)
               && _parser.recv("OK");
    }
    if (success) {
        success = _parser.send("AT+CIPMUX=1")
               && _parser.recv("OK");
    }
    _lock.unlock();

    return success;
}

bool ESP32::cre_server(int port)
{
    _lock.lock();
    _parser.setTimeout(3000);
    if (!(_parser.send("AT+CIPSERVER=1,%d", port)
        && _parser.recv("OK"))) {
        _lock.unlock();
        return false;
    }
    _server_act = true;
    _lock.unlock();
    return true;
}

bool ESP32::del_server()
{
    _lock.lock();
    _parser.setTimeout(3000);
    if (!(_parser.send("AT+CIPSERVER=0")
        && _parser.recv("OK"))) {
        _lock.unlock();
        return false;
    }
    _server_act = false;
    _lock.unlock();
    return true;
}

void ESP32::socket_handler(bool connect, int id)
{
    _lock.lock();
    if (connect) {
        _id_bits |= (1 << id);
        if (_server_act) {
            _accept_id.push_back(id);
        }
    } else {
        _id_bits &= ~(1 << id);
        if (_server_act) {
            for (size_t i = 0; i < _accept_id.size(); i++) {
                if (id == _accept_id[i]) {
                    _accept_id.erase(_accept_id.begin() + i);
                }
            }
        }
    }
    _lock.unlock();
}

void ESP32::_connect_handler_0() { socket_handler(true, 0);  }
void ESP32::_connect_handler_1() { socket_handler(true, 1);  }
void ESP32::_connect_handler_2() { socket_handler(true, 2);  }
void ESP32::_connect_handler_3() { socket_handler(true, 3);  }
void ESP32::_connect_handler_4() { socket_handler(true, 4);  }
void ESP32::_closed_handler_0()  { socket_handler(false, 0); }
void ESP32::_closed_handler_1()  { socket_handler(false, 1); }
void ESP32::_closed_handler_2()  { socket_handler(false, 2); }
void ESP32::_closed_handler_3()  { socket_handler(false, 3); }
void ESP32::_closed_handler_4()  { socket_handler(false, 4); }

bool ESP32::accept(int * p_id)
{
    bool ret = false;

    while (!ret) {
        if (!_server_act) {
            break;
        }
        _lock.lock();
        if (!_accept_id.empty()) {
            ret = true;
        } else {
            _parser.setTimeout(2);
            _parser.recv("%*,CONNECT");
            if (!_accept_id.empty()) {
                ret = true;
            }
        }
        if (ret) {
            *p_id = _accept_id[0];
            _accept_id.erase(_accept_id.begin());
        }
        _lock.unlock();
        Thread::wait(1);
    }

    return ret;
}

bool ESP32::reset(void)
{
    for (int i = 0; i < 2; i++) {
        _lock.lock();
        _parser.setTimeout(1500);
        if (_parser.send("AT+RST")
            && _parser.recv("OK")) {
            _serial.baud(115200);
            _parser.recv("ready");

            if (_parser.send("AT+UART=230400,8,1,0,0")
                && _parser.recv("OK")) {
                _serial.baud(230400);
            }

            _lock.unlock();
            return true;
        }
        _lock.unlock();
    }

    return false;
}

bool ESP32::dhcp(bool enabled, int mode)
{
    bool ret;

    //only 3 valid modes
    if (mode < 0 || mode > 2) {
        return false;
    }

    _lock.lock();
    _parser.setTimeout(10000);
    ret = _parser.send("AT+CWDHCP=%d,%d", enabled?1:0, mode)
       && _parser.recv("OK");
    _lock.unlock();
    return ret;
}

bool ESP32::connect(const char *ap, const char *passPhrase)
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CWJAP=\"%s\",\"%s\"", ap, passPhrase)
       && _parser.recv("OK");
    _lock.unlock();
    return ret;
}

bool ESP32::get_ssid(const char *ap)
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(500);
    ret = _parser.send("AT+CWJAP?")
       && _parser.recv("+CWJAP:\"%33[^\"]\",", ap)
       && _parser.recv("OK");
    _lock.unlock();
    return ret;
}

bool ESP32::disconnect(void)
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CWQAP") && _parser.recv("OK");
    _lock.unlock();
    return ret;
}

const char *ESP32::getIPAddress(void)
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CIFSR")
       && _parser.recv("+CIFSR:STAIP,\"%15[^\"]\"", _ip_buffer)
       && _parser.recv("OK");
    _lock.unlock();
    if (!ret) {
        return 0;
    }
    return _ip_buffer;
}

const char *ESP32::getMACAddress(void)
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CIFSR")
       && _parser.recv("+CIFSR:STAMAC,\"%17[^\"]\"", _mac_buffer)
       && _parser.recv("OK");
    _lock.unlock();

    if (!ret) {
        return 0;
    }
    return _mac_buffer;
}

const char *ESP32::getGateway()
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CIPSTA?")
       && _parser.recv("+CIPSTA:gateway:\"%15[^\"]\"", _gateway_buffer)
       && _parser.recv("OK");
    _lock.unlock();

    if (!ret) {
        return 0;
    }
    return _gateway_buffer;
}

const char *ESP32::getNetmask()
{
    bool ret;

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CIPSTA?")
       && _parser.recv("+CIPSTA:netmask:\"%15[^\"]\"", _netmask_buffer)
       && _parser.recv("OK");
    _lock.unlock();

    if (!ret) {
        return 0;
    }
    return _netmask_buffer;
}

int8_t ESP32::getRSSI()
{
    bool ret;
    int8_t rssi;
    char ssid[33];
    char bssid[18];

    _lock.lock();
    _parser.setTimeout(3000);
    ret = _parser.send("AT+CWJAP?")
       && _parser.recv("+CWJAP:\"%32[^\"]\",\"%17[^\"]\"", ssid, bssid)
       && _parser.recv("OK");
    if (!ret) {
        _lock.unlock();
        return 0;
    }

    ret = _parser.send("AT+CWLAP=\"%s\",\"%s\"", ssid, bssid)
       && _parser.recv("+CWLAP:(%*d,\"%*[^\"]\",%hhd,", &rssi)
       && _parser.recv("OK");
    _lock.unlock();

    if (!ret) {
        return 0;
    }

    return rssi;
}

bool ESP32::isConnected(void)
{
    return getIPAddress() != 0;
}

int ESP32::scan(WiFiAccessPoint *res, unsigned limit)
{
    unsigned cnt = 0;
    nsapi_wifi_ap_t ap;

    _lock.lock();
    _parser.setTimeout(5000);
    if (!_parser.send("AT+CWLAP")) {
        _lock.unlock();
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    while (recv_ap(&ap)) {
        if (cnt < limit) {
            res[cnt] = WiFiAccessPoint(ap);
        }

        cnt++;
        if ((limit != 0) && (cnt >= limit)) {
            break;
        }
        _parser.setTimeout(500);
    }
    _parser.setTimeout(10);
    _parser.recv("OK");
    _lock.unlock();

    return cnt;
}

bool ESP32::open(const char *type, int id, const char* addr, int port)
{
    bool ret;

    //IDs only 0-4
    if (id > 4) {
        return false;
    }

    _lock.lock();
    _parser.setTimeout(500);
    ret = _parser.send("AT+CIPSTART=%d,\"%s\",\"%s\",%d", id, type, addr, port)
       && _parser.recv("OK");
    _lock.unlock();

    return ret;
}

bool ESP32::send(int id, const void *data, uint32_t amount)
{
    uint32_t send_size;
    bool ret;
    int error_cnt = 0;
    int index = 0;

    if (amount == 0) {
        return true;
    }

    //May take a second try if device is busy
    while (error_cnt < 2) {
        if ((_id_bits & (1 << id)) == 0) {
            return false;
        }
        send_size = amount;
        if (send_size > 2048) {
            send_size = 2048;
        }
        _lock.lock();
        _parser.setTimeout(5000);
        ret = _parser.send("AT+CIPSEND=%d,%d", id, send_size)
           && _parser.recv(">")
           && (_parser.write((char*)data + index, (int)send_size) >= 0)
           && _parser.recv("SEND OK");
        _lock.unlock();
        if (ret) {
            amount -= send_size;
            index += send_size;
            if (amount == 0) {
                return true;
            }
        } else {
            error_cnt++;
        }
    }

    return false;
}

void ESP32::_packet_handler()
{
    int id;
    uint32_t amount;
    int tmp_timeout;

    // parse out the packet
    if (!_parser.recv(",%d,%d:", &id, &amount)) {
        return;
    }

    struct packet *packet = (struct packet*)malloc(
            sizeof(struct packet) + amount);
    if (!packet) {
        return;
    }

    packet->id = id;
    packet->len = amount;
    packet->next = 0;
    packet->index = 0;

    tmp_timeout = _parser.getTimeout();
    _parser.setTimeout(100);
    if (!(_parser.read((char*)(packet + 1), amount))) {
        free(packet);
        _parser.setTimeout(tmp_timeout);
        return;
    }
    _parser.setTimeout(tmp_timeout);

    // append to packet list
    *_packets_end = packet;
    _packets_end = &packet->next;
}

int32_t ESP32::recv(int id, void *data, uint32_t amount)
{
    bool retry = false;

    while (true) {
        // check if any packets are ready for us
        for (struct packet **p = &_packets; *p; p = &(*p)->next) {
            if ((*p)->id == id) {
                struct packet *q = *p;

                if (q->len <= amount) { // Return and remove full packet
                    memcpy(data, (uint8_t*)(q+1) + q->index, q->len);

                    if (_packets_end == &(*p)->next) {
                        _packets_end = p;
                    }
                    *p = (*p)->next;

                    uint32_t len = q->len;
                    free(q);
                    return len;
                } else { // return only partial packet
                    memcpy(data, (uint8_t*)(q+1) + q->index, amount);

                    q->len -= amount;
                    q->index += amount;

                    return amount;
                }
            }
        }

        // Wait for inbound packet
        _lock.lock();
        _parser.setTimeout(2);
        if (!_parser.recv("OK")) {
            _lock.unlock();
            if (retry) {
                if ((_id_bits & (1 << id)) == 0) {
                    return -2;
                } else {
                    return -1;
                }
            }
            retry = true;
        }
        _lock.unlock();
    }
}

bool ESP32::close(int id)
{
    //May take a second try if device is busy
    for (unsigned i = 0; i < 2; i++) {
        if ((_id_bits & (1 << id)) == 0) {
            return true;
        }
        _lock.lock();
        _parser.setTimeout(500);
        if (_parser.send("AT+CIPCLOSE=%d", id)
            && _parser.recv("OK")) {
            _lock.unlock();
            return true;
        }
        _lock.unlock();
    }

    return false;
}

void ESP32::setTimeout(uint32_t timeout_ms)
{
    _parser.setTimeout(timeout_ms);
}

bool ESP32::readable()
{
    return _serial.readable();
}

bool ESP32::writeable()
{
    return _serial.writeable();
}

void ESP32::attach(Callback<void()> func)
{
    _serial.attach(func);
}

bool ESP32::recv_ap(nsapi_wifi_ap_t *ap)
{
    int sec;
    bool ret = _parser.recv("+CWLAP:(%d,\"%32[^\"]\",%hhd,\"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx\",%d)", &sec, ap->ssid,
                            &ap->rssi, &ap->bssid[0], &ap->bssid[1], &ap->bssid[2], &ap->bssid[3], &ap->bssid[4],
                            &ap->bssid[5], &ap->channel);

    ap->security = sec < 5 ? (nsapi_security_t)sec : NSAPI_SECURITY_UNKNOWN;

    return ret;
}
