/* Host test: NMEA 2000 -> NMEA 0183 converter and AIS packer.
 * Builds n2k_to_0183.cpp, ais0183.cpp, and nmea.cpp with clang++ against
 * the NMEA2000 library's portable message code. No board, no CAN.
 * Sentences go to a file that ais_check.py decodes independently. */

#include "ais0183.h"
#include "n2k_to_0183.h"
#include "nmea.h"

#include <N2kMessages.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static uint32_t g_now = 1000;
extern "C" uint32_t millis(void) { return g_now; }

static std::vector<std::string> g_out;
static FILE *g_file = nullptr;
static int g_fail = 0;
static int g_pass = 0;

static void sink(const char *sentence, size_t, void *)
{
    g_out.emplace_back(sentence);
    if (g_file != nullptr) {
        fprintf(g_file, "%s\n", sentence);
    }
}

static void note(const char *text)
{
    if (g_file != nullptr) {
        fprintf(g_file, "%s\n", text);
    }
}

static std::string body(const std::string &s)
{
    const size_t star = s.rfind('*');
    return star == std::string::npos ? s : s.substr(0, star);
}

static void expect_eq(const char *what, const std::string &got, const std::string &want)
{
    if (got == want) {
        ++g_pass;
    } else {
        ++g_fail;
        printf("FAIL %s\n  got  %s\n  want %s\n", what, got.c_str(), want.c_str());
    }
}

static void expect_true(const char *what, bool ok)
{
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        printf("FAIL %s\n", what);
    }
}

static const std::string *find(const char *prefix)
{
    for (const auto &s : g_out) {
        if (s.compare(0, strlen(prefix), prefix) == 0) {
            return &s;
        }
    }
    return nullptr;
}

static void expect_sentence(const char *prefix, const char *want_body)
{
    const std::string *s = find(prefix);
    expect_eq(prefix, s ? body(*s) : std::string("(missing)"), want_body);
}

static void expect_absent(const char *prefix)
{
    const std::string *s = find(prefix);
    expect_eq((std::string("no ") + prefix).c_str(), s ? *s : std::string("(none)"), "(none)");
}

static void feed(const tN2kMsg &msg) { n2k_to_0183_handle(msg); }

static double rad(double deg) { return deg * M_PI / 180.0; }

/* ---- AIS packer against published reference sentences ---- */

static void test_ais_packer(void)
{
    /* gpsd AIVDM.txt example: type 1, MMSI 477553000, moored. */
    AisBits b;
    b.clear();
    b.put(1, 6);
    b.put(0, 2);
    b.put(477553000, 30);
    b.put(5, 4);
    b.put_signed(0, 8);
    b.put(0, 10);
    b.put(0, 1);
    b.put_signed(static_cast<int32_t>(lround(-122.345833333 * 600000.0)), 28);
    b.put_signed(static_cast<int32_t>(lround(47.582833333 * 600000.0)), 27);
    b.put(510, 12);
    b.put(181, 9);
    b.put(15, 6);
    b.put(0, 2);
    b.put(0, 3);
    b.put(0, 1);
    b.put(149208, 19);
    char out[3][84];
    int n = ais_finish(b, 'B', false, out, 3);
    expect_true("type 1 one sentence", n == 1);
    expect_eq("type 1 reference", n == 1 ? out[0] : "", "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C");

    /* Type 5, EVER DIADEM (two fragments in the reference; we split at 56). */
    b.clear();
    b.put(5, 6);
    b.put(0, 2);
    b.put(351759000, 30);
    b.put(0, 2);
    b.put(9134270, 30);
    b.put_text("3FOF8  ", 7);
    b.put_text("EVER DIADEM         ", 20);
    b.put(70, 8);
    b.put(225, 9);
    b.put(70, 9);
    b.put(1, 6);
    b.put(31, 6);
    b.put(1, 4);
    b.put(5, 4);
    b.put(15, 5);
    b.put(14, 5);
    b.put(0, 6);
    b.put(122, 8);
    b.put_text("NEW YORK            ", 20);
    b.put(0, 1);
    b.put(0, 1);
    n = ais_finish(b, 'A', false, out, 3);
    expect_true("type 5 two fragments", n == 2);
    std::string payload;
    for (int i = 0; i < n; ++i) {
        std::string f = out[i];
        expect_true("type 5 fragment <= 82 chars", f.size() <= 82);
        size_t c = 0;
        int field = 0;
        for (size_t k = 0; k < f.size(); ++k) {
            if (f[k] == ',') {
                ++field;
                if (field == 5) {
                    c = k + 1;
                } else if (field == 6) {
                    payload += f.substr(c, k - c);
                }
            }
        }
    }
    expect_eq("type 5 reference payload", payload,
              "55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp888888888880");
    expect_true("type 5 last fragment fill 2", n == 2 && body(out[1]).substr(body(out[1]).size() - 2) == ",2");
    expect_true("type 5 shared message id", n == 2 && out[0][11] == out[1][11] && out[0][11] >= '0' &&
                                                 out[0][11] <= '9');
    for (int i = 0; i < n; ++i) {
        note(out[i]);
    }

    /* Own ship goes out as !AIVDO. */
    b.clear();
    b.put(1, 6);
    b.put(0, 162);
    n = ais_finish(b, 'A', true, out, 3);
    expect_true("own ship talker", n == 1 && strncmp(out[0], "!AIVDO,1,1,,A,", 14) == 0);
}

/* ---- PGN -> 0183 ---- */

static void test_gnss(void)
{
    tN2kMsg m;
    g_out.clear();
    g_now = 10000;
    SetN2kPGN126992(m, 1, 20743, 45296.0, N2ktimes_GPS);
    feed(m);
    SetN2kPGN129029(m, 1, 20743, 45296.0, 49.2741666667, -123.113, 3.5, N2kGNSSt_GPS, N2kGNSSm_GNSSfix, 9,
                    0.9, 1.5, -17.0);
    feed(m);
    SetN2kPGN129026(m, 1, N2khr_true, rad(30.0), 6.0 / 1.9438444924406046);
    feed(m);
    n2k_to_0183_poll();
    expect_sentence("$GPRMC", "$GPRMC,123456.00,A,4916.4500,N,12306.7800,W,6.0,30.0,171026,,,A");
    expect_sentence("$GPGGA", "$GPGGA,123456.00,4916.4500,N,12306.7800,W,1,09,0.9,3.5,M,-17.0,M,,");
    expect_sentence("$GPGLL", "$GPGLL,4916.4500,N,12306.7800,W,123456.00,A,A");
    expect_sentence("$GPVTG", "$GPVTG,30.0,T,,M,6.0,N,11.1,K,A");

    /* 1 Hz: a second poll in the same second sends nothing. */
    g_out.clear();
    g_now += 400;
    n2k_to_0183_poll();
    expect_true("GNSS rate limited to 1 Hz", g_out.empty());

    /* Minutes that round to 60 carry into the degree. */
    g_out.clear();
    g_now += 1000;
    SetN2kPGN129025(m, 48.9999999, -0.9999999);
    feed(m);
    n2k_to_0183_poll();
    const std::string *rmc = find("$GPRMC");
    expect_true("RMC 48.9999999 -> 4900.0000,N", rmc && rmc->find(",4900.0000,N,") != std::string::npos);
    expect_true("RMC -0.9999999 -> 00100.0000,W", rmc && rmc->find(",00100.0000,W,") != std::string::npos);

    /* GNSS stops: nothing stale is repeated. */
    g_out.clear();
    g_now += 4000;
    n2k_to_0183_poll();
    expect_true("stale GNSS not repeated", g_out.empty());

    /* 129025 alone: RMC/GLL, no invented GGA, no frozen clock or speed. */
    g_out.clear();
    g_now += 1000;
    SetN2kPGN129025(m, -33.8568, 151.2153);
    feed(m);
    n2k_to_0183_poll();
    expect_sentence("$GPRMC", "$GPRMC,,A,3351.4080,S,15112.9180,E,,,,,,A");
    expect_absent("$GPGGA");
    expect_absent("$GPVTG");

    /* 129029 says no fix: RMC void, GGA quality 0. */
    g_out.clear();
    g_now += 1000;
    SetN2kPGN129029(m, 2, 20743, 45300.0, -33.8568, 151.2153, N2kDoubleNA, N2kGNSSt_GPS, N2kGNSSm_noGNSS, 0,
                    N2kDoubleNA, N2kDoubleNA, N2kDoubleNA);
    feed(m);
    n2k_to_0183_poll();
    expect_sentence("$GPRMC", "$GPRMC,123500.00,V,3351.4080,S,15112.9180,E,,,171026,,,N");
    expect_sentence("$GPGGA", "$GPGGA,123500.00,3351.4080,S,15112.9180,E,0,00,,,M,,M,,");
}

static void test_instruments(void)
{
    tN2kMsg m;
    g_out.clear();
    g_now = 100000;

    SetN2kPGN128267(m, 1, 12.34, N2kDoubleNA);
    feed(m);
    expect_sentence("$SDDPT", "$SDDPT,12.3,0.0,");
    g_out.clear();
    SetN2kPGN128267(m, 1, 15.0, 0.5);
    feed(m);
    expect_true("DPT rate limited", g_out.empty());
    g_now += 1000;
    SetN2kPGN128267(m, 1, N2kDoubleNA, 0.5);
    feed(m);
    expect_true("DPT not available -> nothing", g_out.empty());

    SetN2kPGN130306(m, 1, 10.0 / 1.9438444924406046, rad(330.0), N2kWind_Apparent);
    feed(m);
    expect_sentence("$WIMWV", "$WIMWV,330.0,R,10.0,N,A");
    g_out.clear();
    g_now += 1000;
    SetN2kPGN130306(m, 1, 10.0 / 1.9438444924406046, rad(270.0), N2kWind_True_North);
    feed(m);
    expect_absent("$WIMWV");
    expect_sentence("$WIMWD", "$WIMWD,270.0,T,,M,10.0,N,5.1,M");
    g_out.clear();
    g_now += 1000;
    SetN2kPGN130306(m, 1, 8.0 / 1.9438444924406046, rad(45.0), N2kWind_True_water);
    feed(m);
    expect_sentence("$WIMWV", "$WIMWV,45.0,T,8.0,N,A");

    g_out.clear();
    SetN2kPGN127250(m, 1, rad(123.0), N2kDoubleNA, rad(-15.0), N2khr_magnetic);
    feed(m);
    expect_sentence("$HCHDG", "$HCHDG,123.0,,,15.0,W");
    g_out.clear();
    g_now += 1000;
    SetN2kPGN127250(m, 1, rad(359.99), N2kDoubleNA, N2kDoubleNA, N2khr_true);
    feed(m);
    expect_sentence("$HEHDT", "$HEHDT,360.0,T");

    g_out.clear();
    SetN2kPGN128259(m, 1, 5.0 / 1.9438444924406046);
    feed(m);
    expect_sentence("$VWVHW", "$VWVHW,,T,,M,5.0,N,9.3,K");
}

static void test_ais_convert(void)
{
    tN2kMsg m;
    g_out.clear();

    note("# expect type=1;mmsi=477553000;status=5;lon=-122.3458;lat=47.5828;cog=51.0;heading=181;second=15;"
         "sog=0.0;rot=0;_talker=!AIVDM;_channel=B");
    SetN2kPGN129038(m, 1, N2kaisr_Initial, 477553000, 47.582833333, -122.345833333, false, false, 15,
                    rad(51.0), 0.0, N2kaischannel_B_VDL_reception, rad(181.0), 0.0, N2kaisns_Moored);
    feed(m);
    expect_true("129038 -> one !AIVDM", g_out.size() == 1 && g_out[0].compare(0, 15, "!AIVDM,1,1,,B,1") == 0);
    /* Same fields as the reference sentence; only radio status differs. */
    expect_true("129038 matches reference payload", !g_out.empty() &&
                                                       g_out[0].compare(14, 23, "177KQJ5000G?tO`K>RA1wUb") == 0);

    note("# expect type=1;mmsi=316001234;rot=-128;heading=0;sog=102.2;cog=0.0;_talker=!AIVDO;_channel=A");
    SetN2kPGN129038(m, 1, N2kaisr_Initial, 316001234, -12.5, 45.25, true, true, 30, rad(359.99), 60.0,
                    N2kaischannel_A_VDL_transmission, rad(359.7), N2kDoubleNA, N2kaisns_Under_Way_Motoring);
    feed(m);

    note("# expect type=18;mmsi=338123456;lat=48.4284;lon=-123.3656;sog=5.0;cog=180.0;heading=511;cs=1;"
         "msg22=1;_channel=B");
    SetN2kPGN129039(m, 18, N2kaisr_Initial, 338123456, 48.4284, -123.3656, false, false, 12, rad(180.0),
                    5.0 / 1.9438444924406046, N2kaischannel_B_VDL_reception, N2kDoubleNA, N2kaisunit_ClassB_CS,
                    true, false, true, true, N2kaismode_Autonomous, true);
    feed(m);

    note("# expect type=5;mmsi=351759000;imo=9134270;callsign=3FOF8;shipname=EVER DIADEM;shiptype=70;"
         "to_bow=225;to_stern=70;to_port=1;to_starboard=31;epfd=1;month=5;day=15;hour=14;minute=0;"
         "draught=12.2;destination=NEW YORK;dte=0");
    SetN2kPGN129794(m, 5, N2kaisr_Initial, 351759000, 9134270, "3FOF8", "EVER DIADEM", 70, 295.0, 32.0, 31.0,
                    225.0, 20588, 14 * 3600.0, 12.2, "NEW YORK", N2kaisv_ITU_R_M_1371_1,
                    static_cast<tN2kGNSStype>(1), N2kaisdte_Ready, N2kaischannel_A_VDL_reception);
    feed(m);

    note("# expect type=24;mmsi=338123456;partno=0;shipname=WIND DANCER");
    SetN2kPGN129809(m, 24, N2kaisr_Initial, 338123456, "WIND DANCER");
    feed(m);
    note("# expect type=24;mmsi=338123456;partno=1;shiptype=36;vendorid=GRMN;callsign=CFA1234;to_bow=3;"
         "to_stern=9;to_port=2;to_starboard=2");
    SetN2kPGN129810(m, 24, N2kaisr_Initial, 338123456, 36, "GRMN", "CFA1234", 12.0, 4.0, 2.0, 3.0, 0);
    feed(m);

    /* 129040 has no library setter. Build it the way canboat lays it out. */
    note("# expect type=19;mmsi=338654321;lat=48.5;lon=-123.25;sog=6.5;cog=90.0;heading=88;shipname=SEA ROVER;"
         "shiptype=37;to_bow=3;to_stern=9;to_port=2;to_starboard=2;epfd=1;_channel=B");
    m.SetPGN(129040L);
    m.Priority = 4;
    m.AddByte(19);
    m.Add4ByteUInt(338654321);
    m.Add4ByteDouble(-123.25, 1e-07);
    m.Add4ByteDouble(48.5, 1e-07);
    m.AddByte(static_cast<unsigned char>(0 | (0 << 1) | (20 << 2)));
    m.Add2ByteUDouble(rad(90.0), 1e-04);
    m.Add2ByteUDouble(6.5 / 1.9438444924406046, 0.01);
    m.AddByte(0xff);
    m.AddByte(0xff);
    m.AddByte(37);
    m.Add2ByteUDouble(rad(88.0), 1e-04);
    m.AddByte(static_cast<unsigned char>((1 << 4) | 0x0f));
    m.Add2ByteUDouble(12.0, 0.1);
    m.Add2ByteUDouble(4.0, 0.1);
    m.Add2ByteUDouble(2.0, 0.1);
    m.Add2ByteUDouble(3.0, 0.1);
    m.AddStr("SEA ROVER", 20);
    m.AddByte(static_cast<unsigned char>(N2kaischannel_B_VDL_reception << 6));
    m.AddByte(static_cast<unsigned char>((N2kaischannel_B_VDL_reception >> 2) | 0xf8));
    feed(m);

    m.SetPGN(129808L);
    m.AddByte(0x70);
    m.AddByte(0x01);
    feed(m);
    const std::string *dsc = find("$PLMYC,DSC,");
    expect_true("129808 -> proprietary $PLMYC,DSC", dsc != nullptr);
    expect_absent("$CDDSE");

    for (const auto &s : g_out) {
        expect_true("AIS / DSC <= 82 chars", s.size() <= kNmeaMaxSentence);
    }
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "n2k_0183_out.txt";
    g_file = fopen(path, "w");
    nmea_init();
    nmea_set_sink(sink, nullptr);

    test_ais_packer();
    test_gnss();
    test_instruments();
    test_ais_convert();

    expect_true("no sentence rejected by the 0183 checksum path", nmea_bad_count() == 0);
    if (g_file != nullptr) {
        fclose(g_file);
    }
    printf("host test: %d passed, %d failed (sentences in %s)\n", g_pass, g_fail, path);
    return g_fail == 0 ? 0 : 1;
}
