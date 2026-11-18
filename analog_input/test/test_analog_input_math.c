#include <math.h>
#include <stdio.h>
#include <string.h>

#include "analog_input.h"

/*----------------------------------------------------------------------------*/
/* Test harness                                                               */
/*----------------------------------------------------------------------------*/

static int checks;
static int failures;

/** @brief Record one boolean check and report the expression on failure. */
#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if ( !(cond) ) {                                                     \
            failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while ( 0 )

/*----------------------------------------------------------------------------*/
/* PT100 oracle: IEC 60751 Callendar-Van Dusen in double precision            */
/*----------------------------------------------------------------------------*/

/** @brief CVD coefficients of a 100 ohm element, IEC 60751. */
static const double CVD_R0 = 100.0;
static const double CVD_A  = 3.9083e-3;
static const double CVD_B  = -5.775e-7;
static const double CVD_C  = -4.183e-12;

/** @brief Resistance of a PT100 element at a temperature, in ohms. */
static double cvd_resistance(double temperature_c)
{
    double resistance;

    resistance = CVD_R0 * (1.0 + (CVD_A * temperature_c)
                               + (CVD_B * temperature_c * temperature_c));
    if ( temperature_c < 0.0 ) {
        resistance += CVD_R0 * CVD_C * (temperature_c - 100.0)
                      * temperature_c * temperature_c * temperature_c;
    }
    return resistance;
}

/*----------------------------------------------------------------------------*/
/* Thermocouple anchors: NIST ITS-90 table values, its90.nist.gov             */
/*----------------------------------------------------------------------------*/

/** @brief One forward-model anchor: temperature and the published emf. */
typedef struct {
    float temperature_c;
    float emf_mv;
    float tolerance_mv; /**< Zero selects the default table tolerance. */
} tc_anchor_t;

/** @brief Default anchor tolerance: table rounding plus float evaluation. */
#define TC_TABLE_TOL_MV 0.005f

static const tc_anchor_t K_ANCHORS[] = {
    { -200.0f, -5.891f, 0.0f }, { -100.0f, -3.554f, 0.0f },
    { 0.0f, 0.000f, 0.0f },    { 100.0f, 4.096f, 0.0f },
    { 400.0f, 16.397f, 0.0f }, { 500.0f, 20.644f, 0.0f },
    { 1000.0f, 41.276f, 0.0f }, { 1372.0f, 54.886f, 0.0f }
};
static const tc_anchor_t E_ANCHORS[] = {
    { -200.0f, -8.825f, 0.0f }, { -100.0f, -5.237f, 0.0f },
    { 100.0f, 6.319f, 0.0f },   { 600.0f, 45.093f, 0.0f },
    { 1000.0f, 76.373f, 0.0f }
};
static const tc_anchor_t J_ANCHORS[] = {
    { -100.0f, -4.633f, 0.0f }, { 100.0f, 5.269f, 0.0f },
    { 500.0f, 27.393f, 0.0f }, { 760.0f, 42.919f, 0.0f },
    { 1200.0f, 69.553f, 0.0f }
};
static const tc_anchor_t N_ANCHORS[] = {
    { -100.0f, -2.407f, 0.0f }, { 100.0f, 2.774f, 0.0f },
    { 500.0f, 16.748f, 0.0f },  { 1300.0f, 47.513f, 0.0f },
    /*
     * Tight on purpose: the negative-range polynomial must end at the
     * degree-8 term; two spurious higher-order terms borrowed from the
     * positive range (a transcription defect class) shift this point by
     * about 1.6 uV while the correct float evaluation stays within the
     * table rounding of 0.5 uV.
     */
    { -270.0f, -4.345f, 0.001f }
};
static const tc_anchor_t T_ANCHORS[] = {
    { -200.0f, -5.603f, 0.0f }, { -100.0f, -3.379f, 0.0f },
    { 100.0f, 4.279f, 0.0f },   { 300.0f, 14.862f, 0.0f },
    { 400.0f, 20.872f, 0.0f }
};
static const tc_anchor_t B_ANCHORS[] = {
    { 100.0f, 0.033f, 0.0f },   { 500.0f, 1.242f, 0.0f },
    { 700.0f, 2.431f, 0.0f },   { 1000.0f, 4.834f, 0.0f },
    { 630.615f, 1.979f, 0.0f }, { 1820.0f, 13.820f, 0.0f }
};
static const tc_anchor_t R_ANCHORS[] = {
    { -50.0f, -0.226f, 0.0f },  { 100.0f, 0.647f, 0.0f },
    { 1000.0f, 10.506f, 0.0f }, { 1768.0f, 21.101f, 0.0f }
};
static const tc_anchor_t S_ANCHORS[] = {
    { -50.0f, -0.236f, 0.0f },  { 100.0f, 0.646f, 0.0f },
    { 1000.0f, 9.587f, 0.0f },  { 1768.0f, 18.693f, 0.0f }
};

/** @brief Run one anchor set against the forward model. */
static void check_anchors(analog_input_thermocouple_type_t type,
                          const tc_anchor_t* anchors,
                          size_t count)
{
    size_t i;

    for ( i = 0U; i < count; ++i ) {
        float emf_mv;
        float tolerance;

        if ( analog_input_thermocouple_temperature_to_mv(
                 type, anchors[i].temperature_c, &emf_mv) != ANALOG_INPUT_OK ) {
            CHECK(0);
            continue;
        }
        tolerance = (anchors[i].tolerance_mv != 0.0f)
            ? anchors[i].tolerance_mv
            : TC_TABLE_TOL_MV;
        CHECK(fabsf(emf_mv - anchors[i].emf_mv) <= tolerance);
    }
}

/*----------------------------------------------------------------------------*/
/* PT100 inverse against the double-precision oracle                          */
/*----------------------------------------------------------------------------*/

static void test_pt100_inverse(void)
{
    static const float SWEEP[] = {
        -199.0f, -150.0f, -100.0f, -50.0f, -10.0f, -1.0f, 0.0f,
        1.0f, 10.0f, 25.0f, 100.0f, 200.0f, 400.0f, 600.0f, 849.0f
    };
    size_t i;

    for ( i = 0U; i < (sizeof(SWEEP) / sizeof(SWEEP[0])); ++i ) {
        float resistance;
        float temperature;

        resistance = (float)cvd_resistance((double)SWEEP[i]);
        CHECK(analog_input_pt100_resistance_to_temperature(
                  resistance, &temperature) == ANALOG_INPUT_OK);
        CHECK(fabsf(temperature - SWEEP[i]) <= 0.05f);
    }
}

/** @brief Reject resistances outside the standard characteristic. */
static void test_pt100_range(void)
{
    float temperature;

    CHECK(analog_input_pt100_resistance_to_temperature(
              (float)cvd_resistance(850.0) + 0.01f, &temperature)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_pt100_resistance_to_temperature(
              (float)cvd_resistance(-200.0) - 0.01f, &temperature)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_pt100_resistance_to_temperature(100.0f, NULL)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_pt100_resistance_to_temperature(0.0f, &temperature)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_pt100_resistance_to_temperature(
              (0.0f / 0.0f), &temperature) == ANALOG_INPUT_ERR_ARG);
}

/*----------------------------------------------------------------------------*/
/* Thermocouple forward model against the NIST anchors                       */
/*----------------------------------------------------------------------------*/

static void test_thermocouple_anchors(void)
{
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_B, B_ANCHORS,
                  sizeof(B_ANCHORS) / sizeof(B_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_E, E_ANCHORS,
                  sizeof(E_ANCHORS) / sizeof(E_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_J, J_ANCHORS,
                  sizeof(J_ANCHORS) / sizeof(J_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_K, K_ANCHORS,
                  sizeof(K_ANCHORS) / sizeof(K_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_N, N_ANCHORS,
                  sizeof(N_ANCHORS) / sizeof(N_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_R, R_ANCHORS,
                  sizeof(R_ANCHORS) / sizeof(R_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_S, S_ANCHORS,
                  sizeof(S_ANCHORS) / sizeof(S_ANCHORS[0]));
    check_anchors(ANALOG_INPUT_THERMOCOUPLE_TYPE_T, T_ANCHORS,
                  sizeof(T_ANCHORS) / sizeof(T_ANCHORS[0]));
}

/*----------------------------------------------------------------------------*/
/* Thermocouple inverse: forward then inverse over each type's span           */
/*----------------------------------------------------------------------------*/

/** @brief One span definition for the round-trip sweep. */
typedef struct {
    analog_input_thermocouple_type_t type;
    float low;
    float high;
    float step;
} tc_span_t;

/**
 * @brief Assert t -> E(t) -> t' closes within the README accuracy claim.
 *
 * Above -200 C the single-precision result stays within 0.1 C of the
 * reference (README, Accuracy); the sweep uses 0.2 C to stay clear of the
 * solver tolerance. Below -240 C the high-order cancellation of types E
 * and T is documented to cost a few degrees.
 */
static void check_round_trip(const tc_span_t* span)
{
    float temperature;

    for ( temperature = span->low; temperature <= span->high;
          temperature += span->step ) {
        float emf_mv;
        float round_trip;

        if ( analog_input_thermocouple_temperature_to_mv(
                 span->type, temperature, &emf_mv) != ANALOG_INPUT_OK ) {
            CHECK(0);
            continue;
        }
        if ( analog_input_thermocouple_mv_to_temperature(
                 span->type, emf_mv, &round_trip) != ANALOG_INPUT_OK ) {
            CHECK(0);
            continue;
        }
        if ( (temperature < -240.0f)
             && ((span->type == ANALOG_INPUT_THERMOCOUPLE_TYPE_E)
                 || (span->type == ANALOG_INPUT_THERMOCOUPLE_TYPE_T)) ) {
            CHECK(fabsf(round_trip - temperature) <= 5.0f);
        }
        else {
            CHECK(fabsf(round_trip - temperature) <= 0.2f);
        }
    }
}

static void test_thermocouple_round_trip(void)
{
    static const tc_span_t SPANS[] = {
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_B, 100.0f, 1800.0f, 100.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_E, -260.0f, 1000.0f, 50.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_J, -200.0f, 1200.0f, 50.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_K, -250.0f, 1372.0f, 50.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_N, -250.0f, 1300.0f, 50.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_R, -50.0f, 1768.0f, 100.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_S, -50.0f, 1768.0f, 100.0f },
        { ANALOG_INPUT_THERMOCOUPLE_TYPE_T, -260.0f, 400.0f, 25.0f }
    };
    size_t i;

    for ( i = 0U; i < (sizeof(SPANS) / sizeof(SPANS[0])); ++i ) {
        check_round_trip(&SPANS[i]);
    }
}

/**
 * @brief Type B below roughly 40 C: negative emf, clamped by contract.
 *
 * The clamp maps the negative emf to zero, and the solver settles on the
 * curve's origin zero (E(0) is exactly the c0 term): the README's ice
 * point. The channel reads 0 C instead of erroring.
 */
static void test_type_b_clamp(void)
{
    float emf_mv;
    float temperature;

    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_B, 20.0f, &emf_mv)
          == ANALOG_INPUT_OK);
    CHECK(emf_mv < 0.0f);

    CHECK(analog_input_thermocouple_mv_to_temperature(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_B, emf_mv, &temperature)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(temperature) <= 1.0f);
}

/*----------------------------------------------------------------------------*/
/* Argument and range contracts                                               */
/*----------------------------------------------------------------------------*/

static void test_thermocouple_contract(void)
{
    float out;

    /* Unknown model codes, both below and above the table range. */
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE, 100.0f, &out)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              (analog_input_thermocouple_type_t)99, 100.0f, &out)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_thermocouple_mv_to_temperature(
              (analog_input_thermocouple_type_t)99, 1.0f, &out)
          == ANALOG_INPUT_ERR_ARG);

    /* Null and NaN arguments. */
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, 100.0f, NULL)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, (0.0f / 0.0f), &out)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_thermocouple_mv_to_temperature(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, (0.0f / 0.0f), &out)
          == ANALOG_INPUT_ERR_ARG);

    /* Out-of-range temperatures; the range ends themselves are valid. */
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, 1372.1f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, -270.1f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, 1372.0f, &out)
          == ANALOG_INPUT_OK);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_T, 400.1f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_B, -0.1f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_thermocouple_temperature_to_mv(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_B, 1820.1f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);

    /* Out-of-range emf. */
    CHECK(analog_input_thermocouple_mv_to_temperature(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, 54.9f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_thermocouple_mv_to_temperature(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, -6.5f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_thermocouple_mv_to_temperature(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_E, 76.4f, &out)
          == ANALOG_INPUT_ERR_CONVERSION);
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    test_pt100_inverse();
    test_pt100_range();
    test_thermocouple_anchors();
    test_thermocouple_round_trip();
    test_type_b_clamp();
    test_thermocouple_contract();

    printf("test_analog_input_math: %d checks, %d failures\n",
           checks, failures);
    return (failures == 0) ? 0 : 1;
}
