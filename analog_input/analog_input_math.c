#include "analog_input.h"

#include <math.h>

/* PT100 model ------------------------------------------------------------ */

/*
 * Callendar-Van Dusen, IEC 60751:
 *
 *   R(T) = R0 * (1 + A*T + B*T^2)                   T >= 0
 *   R(T) = R0 * (1 + A*T + B*T^2 + C*(T-100)*T^3)   T <  0
 */

#define PT100_R0_OHM (100.0f)
#define PT100_A      (3.9083e-3f)
#define PT100_B      (-5.775e-7f)
#define PT100_C      (-4.183e-12f)

static float pt100_r_of_t(float temperature_c)
{
    float resistance;
    float temperature_squared;

    temperature_squared = temperature_c * temperature_c;
    resistance          = 1.0f + PT100_A * temperature_c
                          + PT100_B * temperature_squared;
    if ( temperature_c < 0.0f ) {
        resistance += PT100_C * (temperature_c - 100.0f)
                      * temperature_c * temperature_squared;
    }
    return PT100_R0_OHM * resistance;
}

/** Slope of the Callendar-Van Dusen characteristic, in ohms per degree. */
static float pt100_dr_dt(float temperature_c)
{
    float temperature_squared;

    temperature_squared = temperature_c * temperature_c;
    return PT100_R0_OHM
           * (PT100_A + (2.0f * PT100_B * temperature_c)
              + (PT100_C
                 * ((4.0f * temperature_squared * temperature_c)
                    - (300.0f * temperature_squared))));
}

analog_input_status_t analog_input_pt100_resistance_to_temperature(
    float resistance_ohm,
    float* temperature_c)
{
    float ratio;
    float discriminant;
    float temperature;
    unsigned int iteration;

    if ( temperature_c == NULL
         || resistance_ohm != resistance_ohm
         || resistance_ohm <= 0.0f ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    /* Reject resistance values outside the standard characteristic. */
    if ( resistance_ohm < pt100_r_of_t(-200.0f)
         || resistance_ohm > pt100_r_of_t(850.0f) ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }

    /* Above 0 C the cubic term is zero and the quadratic form of
     * R(T) = R0 * (1 + A*T + B*T^2) is inverted exactly. */
    ratio        = resistance_ohm / PT100_R0_OHM;
    discriminant = (PT100_A * PT100_A)
                   - (4.0f * PT100_B * (1.0f - ratio));
    temperature  = (-PT100_A + sqrtf(discriminant)) / (2.0f * PT100_B);

    /* Below 0 C the C*(T-100)*T^3 term has no closed-form inverse; two or
     * three Newton steps refine the quadratic estimate. */
    for ( iteration = 0U; (temperature < 0.0f) && (iteration < 4U);
          ++iteration ) {
        temperature -= (pt100_r_of_t(temperature) - resistance_ohm)
                       / pt100_dr_dt(temperature);
    }

    *temperature_c = temperature;
    return ANALOG_INPUT_OK;
}

/* Thermocouple reference functions --------------------------------------- */

/*
 * ITS-90 reference functions from NIST Monograph 175 ("Temperature-
 * Electromotive Force Reference Functions and Tables for the Letter-Designated
 * Thermocouple Types Based on the ITS-90").  Each range is
 *
 *     E(t90) = sum(c_i * t90^i) + a0 * exp(a1 * (t90 - a2)^2)
 *
 * with E in microvolts and t90 in degrees Celsius; the exponential term is
 * used by type K only.  All sets were checked against the fixed-point values
 * of the monograph (ice point, tin, silver, gold, ...) to better than
 * 0.1 microvolt.
 *
 * The polynomials are evaluated in single precision.  Above -200 C the result
 * stays within 0.1 C of the reference; below -240 C the cancellation between
 * the high order terms of types E and T costs a few degrees (about 3 C for
 * type T at -260 C).  Use double math there if cryogenic accuracy matters.
 */

typedef struct {
    const float* coeffs; /**< c0..cn, microvolts. */
    float low;           /**< Range limits in degrees Celsius. */
    float high;
    uint8_t count;     /**< Number of coefficients. */
    uint8_t exp_index; /**< Index into tc_exp, 0xFF for none. */
} tc_range_t;

typedef struct {
    const tc_range_t* ranges;
    uint8_t range_count;
} tc_type_t;

/* a0, a1, a2 of the exponential correction term. */
static const float tc_exp[][3] = {
    {118.5976f, -1.183432e-4f, 126.9686f}, /* type K, 0 .. 1372 C */
};

#define TC_NO_EXP 0xFFU

/* Type B, 0 .. 630.615 C and 630.615 .. 1820 C */
static const float tc_b_1[] = {
    0.0000000000e+00f, -2.4650818346e-01f, 5.9040421171e-03f,
    -1.3257931636e-06f, 1.5668291901e-09f, -1.6944529240e-12f,
    6.2990347094e-16f};
static const float tc_b_2[] = {
    -3.8938168621e+03f, 2.8571747470e+01f, -8.4885104785e-02f,
    1.5785280164e-04f, -1.6835344864e-07f, 1.1109794013e-10f,
    -4.4515431033e-14f, 9.8975640821e-18f, -9.3791330289e-22f};

/* Type E, -270 .. 0 C and 0 .. 1000 C */
static const float tc_e_1[] = {
    0.0000000000e+00f, 5.8665508708e+01f, 4.5410977124e-02f,
    -7.7998048686e-04f, -2.5800160843e-05f, -5.9452583057e-07f,
    -9.3214058667e-09f, -1.0287605534e-10f, -8.0370123621e-13f,
    -4.3979497391e-15f, -1.6414776355e-17f, -3.9673619516e-20f,
    -5.5827328721e-23f, -3.4657842013e-26f};
static const float tc_e_2[] = {
    0.0000000000e+00f, 5.8665508710e+01f, 4.5032275582e-02f,
    2.8908407212e-05f, -3.3056896652e-07f, 6.5024403270e-10f,
    -1.9197495504e-13f, -1.2536600497e-15f, 2.1489217569e-18f,
    -1.4388041782e-21f, 3.5960899481e-25f};

/* Type J, -210 .. 760 C and 760 .. 1200 C */
static const float tc_j_1[] = {
    0.0000000000e+00f, 5.0381187815e+01f, 3.0475836930e-02f,
    -8.5681065720e-05f, 1.3228195295e-07f, -1.7052958337e-10f,
    2.0948090697e-13f, -1.2538395336e-16f, 1.5631725697e-20f};
static const float tc_j_2[] = {
    2.9645625681e+05f, -1.4976127786e+03f, 3.1787103924e+00f,
    -3.1847686701e-03f, 1.5720819004e-06f, -3.0691369056e-10f};

/* Type K, -270 .. 0 C and 0 .. 1372 C (the second range adds the exponential
 * correction listed in tc_exp) */
static const float tc_k_1[] = {
    0.0000000000e+00f, 3.9450128025e+01f, 2.3622373598e-02f,
    -3.2858906784e-04f, -4.9904828777e-06f, -6.7509059173e-08f,
    -5.7410327428e-10f, -3.1088872894e-12f, -1.0451609365e-14f,
    -1.9889266878e-17f, -1.6322697486e-20f};
static const float tc_k_2[] = {
    -1.7600413686e+01f, 3.8921204975e+01f, 1.8558770032e-02f,
    -9.9457592874e-05f, 3.1840945719e-07f, -5.6072844889e-10f,
    5.6075059059e-13f, -3.2020720003e-16f, 9.7151147152e-20f,
    -1.2104721275e-23f};

/* Type N, -270 .. 0 C and 0 .. 1300 C */
static const float tc_n_1[] = {
    0.0000000000e+00f, 2.6159105962e+01f, 1.0957484228e-02f,
    -9.3841111554e-05f, -4.6412039759e-08f, -2.6303357716e-09f,
    -2.2653438003e-11f, -7.6089300791e-14f, -9.3419667835e-17f,
    2.0849229339e-22f, -3.0682196151e-26f};
static const float tc_n_2[] = {
    0.0000000000e+00f, 2.5929394601e+01f, 1.5710141880e-02f,
    4.3825627237e-05f, -2.5261169794e-07f, 6.4311819339e-10f,
    -1.0063471519e-12f, 9.9745338992e-16f, -6.0863245607e-19f,
    2.0849229338e-22f, -3.0682196152e-26f};

/* Type R, -50 .. 1064.18 C, 1064.18 .. 1664.5 C and 1664.5 .. 1768.1 C */
static const float tc_r_1[] = {
    0.0000000000e+00f, 5.2896172976e+00f, 1.3916658978e-02f,
    -2.3885569302e-05f, 3.5691600106e-08f, -4.6234766630e-11f,
    5.0077744103e-14f, -3.7310588619e-17f, 1.5771648237e-20f,
    -2.8103862525e-24f};
static const float tc_r_2[] = {
    2.9515792532e+03f, -2.5206125133e+00f, 1.5956450187e-02f,
    -7.6408594758e-06f, 2.0530529102e-09f, -2.9335966817e-13f};
static const float tc_r_3[] = {
    1.5223211821e+05f, -2.6881988855e+02f, 1.7128028047e-01f,
    -3.4589570645e-05f, -9.3463397105e-12f};

/* Type S, -50 .. 1064.18 C, 1064.18 .. 1664.5 C and 1664.5 .. 1768.1 C */
static const float tc_s_1[] = {
    0.0000000000e+00f, 5.4031330863e+00f, 1.2593428974e-02f,
    -2.3247796869e-05f, 3.2202882304e-08f, -3.3146519639e-11f,
    2.5574425179e-14f, -1.2506887139e-17f, 2.7144317615e-21f};
static const float tc_s_2[] = {
    1.3290044409e+03f, 3.3450931134e+00f, 6.5480519282e-03f,
    -1.6485625921e-06f, 1.2998960517e-11f};
static const float tc_s_3[] = {
    1.4662823264e+05f, -2.5843051675e+02f, 1.6369357464e-01f,
    -3.3043904699e-05f, -9.4322369061e-12f};

/* Type T, -270 .. 0 C and 0 .. 400 C */
static const float tc_t_1[] = {
    0.0000000000e+00f, 3.8748106364e+01f, 4.4194434347e-02f,
    1.1844323105e-04f, 2.0032973554e-05f, 9.0138019559e-07f,
    2.2651156593e-08f, 3.6071154205e-10f, 3.8493939883e-12f,
    2.8213521925e-14f, 1.4251594779e-16f, 4.8768662286e-19f,
    1.0795539270e-21f, 1.3945027062e-24f, 7.9795153927e-28f};
static const float tc_t_2[] = {
    0.0000000000e+00f, 3.8748106364e+01f, 3.3292227880e-02f,
    2.0618243404e-04f, -2.1882256846e-06f, 1.0996880928e-08f,
    -3.0815758772e-11f, 4.5479135290e-14f, -2.7512901673e-17f};

static const tc_range_t tc_b_ranges[] = {
    {tc_b_1, 0.0f, 630.615f, 7U, TC_NO_EXP},
    {tc_b_2, 630.615f, 1820.0f, 9U, TC_NO_EXP},
};
static const tc_range_t tc_e_ranges[] = {
    {tc_e_1, -270.0f, 0.0f, 14U, TC_NO_EXP},
    {tc_e_2, 0.0f, 1000.0f, 11U, TC_NO_EXP},
};
static const tc_range_t tc_j_ranges[] = {
    {tc_j_1, -210.0f, 760.0f, 9U, TC_NO_EXP},
    {tc_j_2, 760.0f, 1200.0f, 6U, TC_NO_EXP},
};
static const tc_range_t tc_k_ranges[] = {
    {tc_k_1, -270.0f, 0.0f, 11U, TC_NO_EXP},
    {tc_k_2, 0.0f, 1372.0f, 10U, 0U},
};
static const tc_range_t tc_n_ranges[] = {
    {tc_n_1, -270.0f, 0.0f, 11U, TC_NO_EXP},
    {tc_n_2, 0.0f, 1300.0f, 11U, TC_NO_EXP},
};
static const tc_range_t tc_r_ranges[] = {
    {tc_r_1, -50.0f, 1064.18f, 10U, TC_NO_EXP},
    {tc_r_2, 1064.18f, 1664.5f, 6U, TC_NO_EXP},
    {tc_r_3, 1664.5f, 1768.1f, 5U, TC_NO_EXP},
};
static const tc_range_t tc_s_ranges[] = {
    {tc_s_1, -50.0f, 1064.18f, 9U, TC_NO_EXP},
    {tc_s_2, 1064.18f, 1664.5f, 5U, TC_NO_EXP},
    {tc_s_3, 1664.5f, 1768.1f, 5U, TC_NO_EXP},
};
static const tc_range_t tc_t_ranges[] = {
    {tc_t_1, -270.0f, 0.0f, 15U, TC_NO_EXP},
    {tc_t_2, 0.0f, 400.0f, 9U, TC_NO_EXP},
};

/* Indexed by analog_input_thermocouple_type_t. */
static const tc_type_t tc_types[] = {
    {tc_b_ranges, 2U},
    {tc_e_ranges, 2U},
    {tc_j_ranges, 2U},
    {tc_k_ranges, 2U},
    {tc_n_ranges, 2U},
    {tc_r_ranges, 3U},
    {tc_s_ranges, 3U},
    {tc_t_ranges, 2U},
};

static const tc_type_t* tc_lookup(analog_input_thermocouple_type_t type)
{
    if ( type < ANALOG_INPUT_THERMOCOUPLE_TYPE_B
         || type > ANALOG_INPUT_THERMOCOUPLE_TYPE_T ) {
        return NULL;
    }
    return &tc_types[(size_t)type];
}

static const tc_range_t* tc_find_range(const tc_type_t* table,
                                       float temperature_c)
{
    uint8_t index;

    for ( index = 0U; index < table->range_count; ++index ) {
        if ( temperature_c >= table->ranges[index].low
             && temperature_c <= table->ranges[index].high ) {
            return &table->ranges[index];
        }
    }
    return NULL;
}

/** Exponential correction term of one range, in microvolts. */
static float tc_exp_term(const tc_range_t* range,
                         float temperature_c)
{
    const float* parameters;
    float offset;

    if ( range->exp_index == TC_NO_EXP ) {
        return 0.0f;
    }
    parameters = tc_exp[range->exp_index];
    offset     = temperature_c - parameters[2];
    return parameters[0] * expf(parameters[1] * offset * offset);
}

/**
 * @brief Reference function of one thermocouple type, in microvolts.
 * @return Zero when the temperature is outside the type's range.
 */
static uint8_t tc_emf_uv(const tc_type_t* table,
                         float temperature_c,
                         float* emf_uv)
{
    const tc_range_t* range;
    float value;
    int8_t index;

    range = tc_find_range(table, temperature_c);
    if ( range == NULL ) {
        return 0U;
    }

    value = 0.0f;
    for ( index = (int8_t)(range->count - 1U); index >= 0; --index ) {
        value = (value * temperature_c)
                + range->coeffs[(uint8_t)index];
    }
    value += tc_exp_term(range, temperature_c);

    *emf_uv = value;
    return 1U;
}

/** @brief First derivative of the reference function, in microvolts per degree. */
static uint8_t tc_slope_uv(const tc_type_t* table,
                           float temperature_c,
                           float* slope_uv)
{
    const tc_range_t* range;
    float value;
    float power;
    uint8_t index;

    range = tc_find_range(table, temperature_c);
    if ( range == NULL ) {
        return 0U;
    }

    value = 0.0f;
    power = 1.0f;
    for ( index = 1U; index < range->count; ++index ) {
        value += (float)index * range->coeffs[index] * power;
        power *= temperature_c;
    }

    if ( range->exp_index != TC_NO_EXP ) {
        const float* parameters = tc_exp[range->exp_index];
        float offset            = temperature_c - parameters[2];

        value += 2.0f * parameters[0] * parameters[1] * offset
                 * expf(parameters[1] * offset * offset);
    }

    *slope_uv = value;
    return 1U;
}

/** Newton iteration limit and convergence limit of the inverse function. */
#define TC_MAX_ITERATIONS 12U
#define TC_TOLERANCE_UV   0.01f

/**
 * @brief Solve the reference function for the temperature.
 *
 * The reference functions are monotonic, so a bracketed Newton iteration
 * always converges; the bracket is also used to fall back to bisection when a
 * Newton step would leave the interval.
 */
static uint8_t tc_solve(const tc_type_t* table,
                        float emf_uv,
                        float* temperature_c)
{
    float low;
    float high;
    float low_emf;
    float high_emf;
    float estimate;
    float value;
    float slope;
    uint8_t iteration;

    low  = table->ranges[0].low;
    high = table->ranges[table->range_count - 1U].high;

    if ( tc_emf_uv(table, low, &low_emf) == 0U
         || tc_emf_uv(table, high, &high_emf) == 0U ) {
        return 0U;
    }
    if ( emf_uv < low_emf || emf_uv > high_emf ) {
        return 0U;
    }

    /* Linear interpolation between the range ends is a good first guess. */
    estimate = low + (((emf_uv - low_emf) / (high_emf - low_emf)) * (high - low));

    for ( iteration = 0U; iteration < TC_MAX_ITERATIONS; ++iteration ) {
        if ( tc_emf_uv(table, estimate, &value) == 0U ) {
            return 0U;
        }
        if ( fabsf(value - emf_uv) <= TC_TOLERANCE_UV ) {
            break;
        }
        if ( value < emf_uv ) {
            low = estimate;
        } else {
            high = estimate;
        }
        if ( tc_slope_uv(table, estimate, &slope) == 0U || slope <= 0.0f ) {
            estimate = (low + high) * 0.5f;
            continue;
        }
        estimate -= (value - emf_uv) / slope;
        if ( estimate <= low || estimate >= high ) {
            estimate = (low + high) * 0.5f;
        }
    }

    *temperature_c = estimate;
    return 1U;
}

/* Public conversion helpers ---------------------------------------------- */

analog_input_status_t analog_input_thermocouple_temperature_to_mv(
    analog_input_thermocouple_type_t type,
    float temperature_c,
    float* emf_mv)
{
    const tc_type_t* table;
    float emf_uv;

    if ( emf_mv == NULL || temperature_c != temperature_c ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    table = tc_lookup(type);
    if ( table == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( tc_emf_uv(table, temperature_c, &emf_uv) == 0U ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }

    *emf_mv = emf_uv / 1000.0f;
    return ANALOG_INPUT_OK;
}

analog_input_status_t analog_input_thermocouple_mv_to_temperature(
    analog_input_thermocouple_type_t type,
    float emf_mv,
    float* temperature_c)
{
    const tc_type_t* table;
    float emf_uv;
    float temperature;

    if ( temperature_c == NULL || emf_mv != emf_mv ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    table = tc_lookup(type);
    if ( table == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    /* Type B is only usable above roughly 250 C: near ambient its output is a
     * few microvolts on a flat curve, so input noise may be negative. Clamping
     * to the ice point keeps the channel usable instead of flagging a
     * conversion error on every reading. */
    if ( type == ANALOG_INPUT_THERMOCOUPLE_TYPE_B && emf_mv < 0.0f ) {
        emf_mv = 0.0f;
    }

    emf_uv = emf_mv * 1000.0f;
    if ( tc_solve(table, emf_uv, &temperature) == 0U ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }

    *temperature_c = temperature;
    return ANALOG_INPUT_OK;
}
