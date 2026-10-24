#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

/**
 * @file analog_input.h
 * @brief Hardware-independent analog input interface.
 *
 * A concrete implementation embeds analog_input_t as the first member of its
 * structure and installs the operation table with analog_input_ctor(). The
 * concrete state can be recovered from the analog_input_t pointer because the
 * base member is first.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status and input types ------------------------------------------------- */

/** @brief Common result codes returned by analog input operations. */
typedef enum {
    ANALOG_INPUT_OK              = 0,  /**< Operation completed. */
    ANALOG_INPUT_ERR_ARG         = -1, /**< Invalid argument. */
    ANALOG_INPUT_ERR_STATE       = -2, /**< Object is not ready. */
    ANALOG_INPUT_ERR_UNSUPPORTED = -3, /**< Operation is not supported. */
    ANALOG_INPUT_ERR_IO          = -4, /**< Hardware access failed. */
    ANALOG_INPUT_ERR_CONVERSION  = -5  /**< Conversion failed. */
} analog_input_status_t;

/** @brief Broad electrical or sensor input category. */
typedef enum {
    ANALOG_INPUT_TYPE_THERMOCOUPLE = 0,
    ANALOG_INPUT_TYPE_PT100        = 1,
    ANALOG_INPUT_TYPE_4_20MA       = 2,
    ANALOG_INPUT_TYPE_0_10V        = 3
} analog_input_type_t;

/** @brief Thermocouple model used by thermocouple conversion. */
typedef enum {
    ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE = -1,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_B    = 0,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_E    = 1,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_J    = 2,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_K    = 3,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_N    = 4,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_R    = 5,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_S    = 6,
    ANALOG_INPUT_THERMOCOUPLE_TYPE_T    = 7
} analog_input_thermocouple_type_t;

/** @brief Result of one wire-break check. */
typedef enum {
    ANALOG_INPUT_WIRE_NORMAL = 0,
    ANALOG_INPUT_WIRE_BROKEN = 1
} analog_input_wire_status_t;

/* Conversion data ------------------------------------------------------- */

/**
 * @brief Type-dependent values required by convert().
 *
 * cold_junction is the thermocouple cold-junction temperature in degrees
 * Celsius. linear_min and linear_max are engineering values corresponding to
 * the low and high input limits for 4-20 mA and 0-10 V inputs. Fields that are
 * irrelevant to the current input type are ignored. The pointer may be NULL
 * when the selected type needs no context.
 */
typedef struct {
    float cold_junction; /**< Thermocouple cold-junction temperature. */
    float linear_min;    /**< Engineering value at the low input limit. */
    float linear_max;    /**< Engineering value at the high input limit. */
} analog_input_conversion_context_t;

/* Sensor conversion helpers -------------------------------------------- */

/**
 * @brief Convert a PT100 resistance to temperature.
 *
 * The helper uses the Callendar-Van Dusen resistance-temperature model of
 * IEC 60751 for a 100 ohm sensor. The input resistance is in ohms and the
 * output temperature is in degrees Celsius. The quadratic form of the model is
 * inverted analytically; below 0 degrees, where the cubic coefficient applies,
 * the result is refined with a few Newton steps. Resistances outside the
 * -200 .. +850 degrees range are rejected instead of being extrapolated.
 */
analog_input_status_t analog_input_pt100_resistance_to_temperature(
    float resistance_ohm,
    float* temperature_c);

/**
 * @brief Convert a thermocouple temperature to its thermoelectric voltage.
 *
 * The input temperature is in degrees Celsius and the output thermoelectric
 * voltage is in millivolts. The function covers the sensor polynomial only;
 * cold-junction compensation is composed by the caller using
 * analog_input_thermocouple_mv_to_temperature().
 */
analog_input_status_t analog_input_thermocouple_temperature_to_mv(
    analog_input_thermocouple_type_t type,
    float temperature_c,
    float* emf_mv);

/**
 * @brief Convert a thermocouple thermoelectric voltage to temperature.
 *
 * The input voltage is in millivolts and the output temperature is in degrees
 * Celsius. The helper performs the inverse sensor conversion only; it does not
 * perform cold-junction compensation.
 */
analog_input_status_t analog_input_thermocouple_mv_to_temperature(
    analog_input_thermocouple_type_t type,
    float emf_mv,
    float* temperature_c);

/* Interface object and operations --------------------------------------- */

typedef struct analog_input analog_input_t;

/**
 * @brief Input configuration stored by the analog input object.
 *
 * thermocouple_type is meaningful only when type is
 * ANALOG_INPUT_TYPE_THERMOCOUPLE. For other input types the implementation
 * normalizes it to ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE.
 *
 * sample_rate_millihz is the requested ADC output data rate in integer
 * millihertz (mHz), where 1 Hz equals 1000 mHz. For example, 2.5 Hz is
 * represented as 2500 mHz. Zero requests the implementation's default rate.
 * Non-zero supported values are implementation-specific and are documented in
 * the concrete implementation's interface or implementation comments.
 */
typedef struct {
    analog_input_type_t type;
    analog_input_thermocouple_type_t thermocouple_type;
    uint32_t sample_rate_millihz;
} analog_input_config_t;

/**
 * @brief Fixed operation table implemented by a concrete hardware driver.
 *
 * read_sample() returns one raw sample code. convert() uses the current input
 * configuration and conversion context to produce an engineering value.
 * Break detection and measurement are independent hardware work modes.
 * Break detection is currently unsupported for 4-20 mA and 0-10 V inputs.
 */
typedef struct {
    /**
     * Select the input type, sensor model, and requested sample rate.
     * Concrete implementations document their supported sample rates and
     * return ANALOG_INPUT_ERR_UNSUPPORTED for unsupported non-zero values.
     *
     * On success, first_conversion_delay_ms receives the delay in milliseconds
     * from the return of configure() until the first new conversion result is
     * guaranteed to be available. Zero means no additional delay is required.
     * Implementations round non-zero delays up to the next whole millisecond.
     * The output is updated only after the configuration succeeds.
     *
     * The operation owns the cached configuration. Before returning success it
     * stores the configuration that is now in effect into self->config, which
     * may differ from the request where a default was resolved. When it fails,
     * it must not leave self->config describing a device that was left in
     * another state.
     */
    analog_input_status_t (*configure)(
        analog_input_t* self,
        const analog_input_config_t* config,
        uint32_t* first_conversion_delay_ms);

    /** Read one raw ADC sample without conversion or filtering. */
    analog_input_status_t (*read_sample)(
        analog_input_t* self,
        int32_t* sample_code);

    /** Convert one raw sample code to an engineering value. */
    analog_input_status_t (*convert)(
        analog_input_t* self,
        int32_t sample_code,
        const analog_input_conversion_context_t* context,
        float* measurement);

    /** Enable the wire-break detection work mode. */
    analog_input_status_t (*enable_break_detection)(
        analog_input_t* self);

    /** Disable the wire-break detection work mode. */
    analog_input_status_t (*disable_break_detection)(
        analog_input_t* self);

    /** Run one wire-break check and return its result immediately. */
    analog_input_status_t (*check_wire_break)(
        analog_input_t* self,
        analog_input_wire_status_t* wire_status);
} analog_input_ops_t;

/**
 * @brief Base object inherited by concrete analog input implementations.
 *
 * Place this member first in the concrete structure. The cached input
 * configuration is maintained by the concrete implementation's configure()
 * operation.
 */
struct analog_input {
    const analog_input_ops_t* ops;
    analog_input_config_t config;
};

/* Construction ----------------------------------------------------------- */

/**
 * @brief Construct an analog input object.
 *
 * Installs the operation table and leaves the object without a configuration,
 * so analog_input_configure() has to succeed before anything else is
 * meaningful.
 *
 * The configuration left behind is one the interface itself rejects: type
 * THERMOCOUPLE combined with thermocouple_type NONE. A concrete implementation
 * can use that pair to recognise an object that was never configured, without
 * keeping a separate flag.
 *
 * Returns ANALOG_INPUT_ERR_ARG when self or ops is NULL, or when any of the six
 * operations is missing.
 */
static inline analog_input_status_t analog_input_ctor(
    analog_input_t* self,
    const analog_input_ops_t* ops)
{
    if ( self == NULL || ops == NULL
         || ops->configure == NULL
         || ops->read_sample == NULL
         || ops->convert == NULL
         || ops->enable_break_detection == NULL
         || ops->disable_break_detection == NULL
         || ops->check_wire_break == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    self->ops = ops;

    /* Nothing has been configured yet, so the stored configuration is not a
     * valid one and must not be trusted. */
    self->config.type                = ANALOG_INPUT_TYPE_THERMOCOUPLE;
    self->config.thermocouple_type   = ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    self->config.sample_rate_millihz = 0U;
    return ANALOG_INPUT_OK;
}

/* Application helpers --------------------------------------------------- */

/**
 * @brief Select the input type and type-specific sensor model.
 *
 * The configuration is stored by the implementation, and only once its hardware
 * operation succeeds. The request is passed on already normalized, so a
 * non-thermocouple input always carries ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE.
 * A zero sample rate requests the implementation's default rate. Concrete
 * implementations validate non-zero sample rates against their documented
 * supported values and return ANALOG_INPUT_ERR_UNSUPPORTED without changing
 * the cached configuration when a value is not supported.
 *
 * first_conversion_delay_ms receives the delay in milliseconds before the first
 * new conversion result is guaranteed to be available. The caller should use it
 * to schedule the first read_sample() event rather than waiting inside the
 * analog input interface. A zero value means read_sample() may be attempted
 * immediately. Implementations round non-zero delays up to the next whole
 * millisecond. The output is updated only after the configuration succeeds.
 */
static inline analog_input_status_t analog_input_configure(
    analog_input_t* self,
    const analog_input_config_t* config,
    uint32_t* first_conversion_delay_ms)
{
    analog_input_config_t normalized;

    if ( self == NULL || config == NULL || first_conversion_delay_ms == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    normalized = *config;
    switch ( normalized.type ) {
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
        case ANALOG_INPUT_TYPE_PT100:
        case ANALOG_INPUT_TYPE_4_20MA:
        case ANALOG_INPUT_TYPE_0_10V:
            break;
        default:
            return ANALOG_INPUT_ERR_ARG;
    }

    if ( normalized.type == ANALOG_INPUT_TYPE_THERMOCOUPLE ) {
        switch ( normalized.thermocouple_type ) {
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_B:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_E:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_J:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_K:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_N:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_R:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_S:
            case ANALOG_INPUT_THERMOCOUPLE_TYPE_T:
                break;
            default:
                return ANALOG_INPUT_ERR_ARG;
        }
    } else {
        normalized.thermocouple_type = ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    }

    if ( self->ops == NULL || self->ops->configure == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }

    return self->ops->configure(self, &normalized, first_conversion_delay_ms);
}

/** Read one raw ADC sample without conversion or filtering. */
static inline analog_input_status_t analog_input_read_sample(
    analog_input_t* self,
    int32_t* sample_code)
{
    if ( self == NULL || sample_code == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->read_sample == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->read_sample(self, sample_code);
}

/**
 * @brief Convert one raw sample code to an engineering value.
 *
 * The conversion uses the input configuration stored in self. context may be
 * NULL for input types that do not require additional conversion values.
 */
static inline analog_input_status_t analog_input_convert(
    analog_input_t* self,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement)
{
    if ( self == NULL || measurement == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->convert == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->convert(self, sample_code, context, measurement);
}

/** Enable the wire-break detection work mode. */
static inline analog_input_status_t analog_input_enable_break_detection(
    analog_input_t* self)
{
    if ( self == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->enable_break_detection == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->enable_break_detection(self);
}

/** Disable the wire-break detection work mode. */
static inline analog_input_status_t analog_input_disable_break_detection(
    analog_input_t* self)
{
    if ( self == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->disable_break_detection == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->disable_break_detection(self);
}

/** Run one wire-break check and return its result immediately. */
static inline analog_input_status_t analog_input_check_wire_break(
    analog_input_t* self,
    analog_input_wire_status_t* wire_status)
{
    if ( self == NULL || wire_status == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->check_wire_break == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->check_wire_break(self, wire_status);
}

#ifdef __cplusplus
}
#endif

#endif
