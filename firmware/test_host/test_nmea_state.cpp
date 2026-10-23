/* Host test: the helm's apparent-wind tile takes only apparent (R) MWV. */
#include "nmea_state.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static uint32_t g_now = 1000;
extern "C" uint32_t millis(void) { return g_now; }

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.05f; }

int main()
{
    nmea_state_init();
    const NmeaInstruments *i = nmea_state_instruments();

    nmea_state_note("$WIMWV,315.0,R,12.0,N,A");
    CHECK(i->have_wind);
    CHECK(near(i->aws_kn, 12.0f));
    CHECK(near(i->awa_deg, -45.0f));

    /* True wind must not touch the apparent tile. */
    g_now = 2000;
    nmea_state_note("$WIMWV,200.0,T,18.0,N,A");
    CHECK(near(i->aws_kn, 12.0f));
    CHECK(near(i->awa_deg, -45.0f));
    CHECK(i->wind_ms == 1000);

    /* Units other than knots are converted. */
    nmea_state_note("$WIMWV,30.0,R,5.0,M,A");
    CHECK(near(i->aws_kn, 9.72f));
    CHECK(near(i->awa_deg, 30.0f));
    nmea_state_note("$WIMWV,30.0,R,18.52,K,A");
    CHECK(near(i->aws_kn, 10.0f));

    /* True wind alone never shows as apparent. */
    nmea_state_init();
    nmea_state_note("$WIMWV,90.0,T,20.0,N,A");
    CHECK(!i->have_wind);

    if (g_fail == 0) std::printf("nmea_state: ok\n");
    return g_fail == 0 ? 0 : 1;
}
