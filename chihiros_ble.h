#pragma once
#include <vector>
#include <cstdint>
#include <initializer_list>

// Hulpfuncties voor het Chihiros BLE protocol.
//
// Alle Chihiros devices (roerder, ventilator, CO2) gebruiken hetzelfde frameformaat:
//
//   [header] [01] [len] [00] [seq] [cmd] [data...] [XOR-CRC]
//
//   header : 0x5a voor auth/RTC/ventilator-mode, 0xa5 voor roerder/drempel
//   len    : totaal aantal bytes in het frame (van 0x01 t/m laatste data-byte)
//   seq    : oplopende teller per device, reset bij elke reconnect
//   cmd    : commando-code (apparaat-specifiek)
//   data   : payload, lengte varieert per commando
//   CRC    : XOR van alle frame-bytes (exclusief header en CRC zelf)

namespace chihiros {

namespace detail {
// Berekent XOR-CRC over het frame en verpakt alles als [header] + frame + [crc]
inline std::vector<uint8_t> wrap(uint8_t header, const std::vector<uint8_t>& frame) {
    uint8_t crc = 0;
    for (auto b : frame) crc ^= b;
    std::vector<uint8_t> p = {header};
    p.insert(p.end(), frame.begin(), frame.end());
    p.push_back(crc);
    return p;
}
} // namespace detail

// Bouwt een volledig BLE pakket voor het opgegeven commando.
// Gebruik: chihiros::pakket(0x5a, 0x04, {0x01}, seq)  →  auth-pakket
inline std::vector<uint8_t> pakket(uint8_t header, uint8_t cmd,
                                    std::initializer_list<uint8_t> data, uint8_t seq) {
    uint8_t len = 5 + data.size(); // 5 vaste bytes: 01 + len + 00 + seq + cmd
    std::vector<uint8_t> frame = {0x01, len, 0x00, seq, cmd};
    frame.insert(frame.end(), data.begin(), data.end());
    return detail::wrap(header, frame);
}

// Stuurt een aan/uit-commando voor één roerder-kanaal (cmd=0x14).
// De andere kanaal-posities krijgen 0xff = "niet aanraken".
// kanaal 0..3 → data-positie [2+kanaal] in het frame.
inline std::vector<uint8_t> roerder_toggle(uint8_t seq, int kanaal, bool aan) {
    std::vector<uint8_t> data(10, 0xff); // 10 bytes, allemaal 0xff
    data[2 + kanaal] = aan ? 0x01 : 0x00;
    std::vector<uint8_t> frame = {0x01, 0x0f, 0x00, seq, 0x14};
    frame.insert(frame.end(), data.begin(), data.end());
    return detail::wrap(0xa5, frame);
}

} // namespace chihiros
