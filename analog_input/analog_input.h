#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

/**
 * @file analog_input.h
 * @brief Hardware-independent analog input interface.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* Status and input types                                                     */
/*----------------------------------------------------------------------------*/

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

/**
 * @brief Result of one wire-break check.
 *
 * A two-terminal sensor can only report that the circuit is open; a sensor
 * whose leads can be told apart reports which one. An implementation returns
 * the most specific value it can determine.
 */
typedef enum {
    ANALOG_INPUT_WIRE_NORMAL        = 0, /**< The circuit is closed. */
    ANALOG_INPUT_WIRE_BROKEN        = 1, /**< Open, kind not identified. */
    ANALOG_INPUT_WIRE_BROKEN_LEAD   = 2, /**< One excitation lead is open. */
    ANALOG_INPUT_WIRE_BROKEN_RETURN = 3  /**< The shared return lead is open. */
} analog_input_wire_status_t;

/*----------------------------------------------------------------------------*/
/* Conversion data                                                            */
/*----------------------------------------------------------------------------*/

/**
 * @brief Type-dependent values required by convert().
 *
 * Fields that are irrelevant to the current input type are ignored, and the
 * pointer may be NULL when the selected type needs no context.
 */
typedef struct {
    float cold_junction; /**< Thermocouple cold-junction temperature. */
    float linear_min;    /**< Engineering value at the low input limit. */
    float linear_max;    /**< Engineering value at the high input limit. */
} analog_input_conversion_context_t;

/*----------------------------------------------------------------------------*/
/* Sensor conversion helpers                                                  */
/*----------------------------------------------------------------------------*/

/**
 * @brief Convert a PT100 resistance to a temperature.
 *
 * @param resistance_ohm Sensor resistance in ohms.
 * @param temperature_c Receives the temperature in degrees Celsius.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, a NaN or a
 *         non-positive resistance, or ANALOG_INPUT_ERR_CONVERSION when the
 *         resistance is outside the -200 .. +850 degrees Celsius range.
 * @note IEC 60751 Callendar-Van Dusen for a 100 ohm element; the model and its
 *       inversion are described in README.md.
 */
analog_input_status_t analog_input_pt100_resistance_to_temperature(
    float resistance_ohm,
    float* temperature_c);

/**
 * @brief Convert a thermocouple temperature to its thermoelectric voltage.
 *
 * @param type Thermocouple model.
 * @param temperature_c Junction temperature in degrees Celsius.
 * @param emf_mv Receives the thermoelectric voltage in millivolts.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, a NaN or an
 *         unknown type, or ANALOG_INPUT_ERR_CONVERSION when the temperature is
 *         outside the type's range.
 * @note Covers the sensor polynomial only; cold-junction compensation is
 *       composed by the caller with
 *       analog_input_thermocouple_mv_to_temperature().
 */
analog_input_status_t analog_input_thermocouple_temperature_to_mv(
    analog_input_thermocouple_type_t type,
    float temperature_c,
    float* emf_mv);

/**
 * @brief Convert a thermocouple thermoelectric voltage to a temperature.
 *
 * @param type Thermocouple model.
 * @param emf_mv Thermoelectric voltage in millivolts.
 * @param temperature_c Receives the temperature in degrees Celsius.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, a NaN or an
 *         unknown type, or ANALOG_INPUT_ERR_CONVERSION when the voltage is
 *         outside the type's range.
 * @note Performs the inverse sensor conversion only; it does not perform
 *       cold-junction compensation. Type B is clamped to the ice point below
 *       zero emf; see README.md.
 */
analog_input_status_t analog_input_thermocouple_mv_to_temperature(
    analog_input_thermocouple_type_t type,
    float emf_mv,
    float* temperature_c);

/*----------------------------------------------------------------------------*/
/* Interface object and operations                                            */
/*----------------------------------------------------------------------------*/

typedef struct analog_input analog_input_t;

/**
 * @brief Input configuration stored by the analog input object.
 *
 * The supported non-zero sample rates are implementation-specific.
 */
typedef struct {
    analog_input_type_t type;                           /**< Input category; selects the conversion model. */
    analog_input_thermocouple_type_t thermocouple_type; /**< Thermocouple model; NONE unless type is THERMOCOUPLE. */
    uint32_t sample_rate_millihz;                       /**< Requested ADC output rate in mHz (1 Hz = 1000 mHz); 0 selects the default. */
} analog_input_config_t;

/**
 * @brief Fixed operation table implemented by a concrete hardware driver.
 *
 * Break detection and measurement are work modes of the same hardware, so they
 * are never active at the same time: enabling detection leaves the measurement
 * mode, and only disabling it again brings the measurement back. Detection is
 * not defined for 4-20 mA and 0-10 V inputs, where an interrupted loop or an
 * absent voltage looks like a valid low reading.
 */
typedef struct {
    /** @brief Select the input type, sensor model and requested sample rate. */
    analog_input_status_t (*configure)(
        analog_input_t* self,
        const analog_input_config_t* config,
        uint32_t* first_conversion_delay_ms);

    /** @brief Read one raw ADC sample without conversion or filtering. */
    analog_input_status_t (*read_sample)(
        analog_input_t* self,
        int32_t* sample_code);

    /** @brief Produce an engineering value from one raw sample code. */
    analog_input_status_t (*convert)(
        analog_input_t* self,
        int32_t sample_code,
        const analog_input_conversion_context_t* context,
        float* measurement);

    /** @brief Enter the wire-break detection work mode. */
    analog_input_status_t (*enable_break_detection)(
        analog_input_t* self,
        uint32_t* first_check_delay_ms);

    /** @brief Leave the detection mode and restore the measurement mode. */
    analog_input_status_t (*disable_break_detection)(
        analog_input_t* self,
        uint32_t* first_conversion_delay_ms);

    /** @brief Report the current wire state without waiting. */
    analog_input_status_t (*check_wire_break)(
        analog_input_t* self,
        analog_input_wire_status_t* wire_status);
} analog_input_ops_t;

/**
 * @brief Base object inherited by concrete analog input implementations.
 *
 * Place this member first in the concrete structure, so the concrete state can
 * be recovered from the analog_input_t pointer.
 */
struct analog_input {
    const analog_input_ops_t* ops; /**< Operation table, installed by analog_input_ctor(). */
    analog_input_config_t config;  /**< Cached configuration, written by the implementation's configure(). */
};

/*----------------------------------------------------------------------------*/
/* Construction                                                               */
/*----------------------------------------------------------------------------*/

/**
 * @brief Construct an analog input object.
 *
 * @param self Storage owned by the caller.
 * @param ops Operation table with all six operations present.
 * @return ANALOG_INPUT_OK, or ANALOG_INPUT_ERR_ARG when self or ops is NULL or
 *         when any of the six operations is missing.
 * @note Leaves the object unconfigured, so analog_input_configure() has to
 *       succeed before anything else is meaningful.
 * @note The configuration left behind is one the interface itself rejects
 *       (THERMOCOUPLE with thermocouple_type NONE), so a concrete
 *       implementation can recognise an object that was never configured
 *       without keeping a separate flag.
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

/*----------------------------------------------------------------------------*/
/* Application helpers                                                        */
/*----------------------------------------------------------------------------*/

/**
 * @brief Select the input type and type-specific sensor model.
 *
 * @param self Analog input object.
 * @param config Requested configuration.
 * @param first_conversion_delay_ms Receives the delay in milliseconds before
 *        the first new conversion result is guaranteed to be available; zero
 *        means read_sample() may be attempted immediately.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, an unknown
 *         type or model, ANALOG_INPUT_ERR_STATE when the object has no
 *         operation table, or ANALOG_INPUT_ERR_UNSUPPORTED for a sample rate
 *         the implementation does not offer.
 * @note The request is normalized first: a non-thermocouple input always
 *       carries ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE, and a zero sample rate
 *       asks for the implementation's default rate.
 * @note Schedule the first read_sample() event from the returned delay instead
 *       of waiting inside the interface. Non-zero delays round up to the next
 *       whole millisecond, and the output is written only after the
 *       configuration succeeds.
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

/**
 * @brief Read one raw ADC sample.
 *
 * @param self Analog input object.
 * @param sample_code Receives the raw ADC code.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, or
 *         ANALOG_INPUT_ERR_STATE when the object has no operation table.
 */
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
 * @param self Analog input object.
 * @param sample_code Raw code from analog_input_read_sample().
 * @param context Type-dependent values; may be NULL when the selected type
 *        needs none.
 * @param measurement Receives the engineering value.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, or
 *         ANALOG_INPUT_ERR_STATE when the object has no operation table.
 * @note Uses the input configuration stored in self.
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

/**
 * @brief Switch to the wire-break detection work mode.
 *
 * @param self Analog input object.
 * @param first_check_delay_ms Receives the delay in milliseconds before
 *        check_wire_break() produces a meaningful result; zero means the check
 *        may run at once.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer,
 *         ANALOG_INPUT_ERR_STATE when no configuration is in effect, or
 *         ANALOG_INPUT_ERR_UNSUPPORTED for input types without a check.
 * @note Schedule the first check from the returned delay instead of waiting
 *       inside the interface. The output is written only when the mode was
 *       entered, and read_sample() and convert() must not be used while it is
 *       active.
 */
static inline analog_input_status_t analog_input_enable_break_detection(
    analog_input_t* self,
    uint32_t* first_check_delay_ms)
{
    if ( self == NULL || first_check_delay_ms == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->enable_break_detection == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->enable_break_detection(self, first_check_delay_ms);
}

/**
 * @brief Return from the detection mode to the measurement mode.
 *
 * @param self Analog input object.
 * @param first_conversion_delay_ms Receives the delay in milliseconds before
 *        read_sample() produces a valid result again.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, or
 *         ANALOG_INPUT_ERR_STATE when the object has no operation table.
 * @note Restores the configuration that configure() last accepted. Schedule
 *       the next measurement event from the returned delay; the output is
 *       written only when the mode was left.
 */
static inline analog_input_status_t analog_input_disable_break_detection(
    analog_input_t* self,
    uint32_t* first_conversion_delay_ms)
{
    if ( self == NULL || first_conversion_delay_ms == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    if ( self->ops == NULL || self->ops->disable_break_detection == NULL ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    return self->ops->disable_break_detection(self, first_conversion_delay_ms);
}

/**
 * @brief Run one wire-break check and return its result immediately.
 *
 * @param self Analog input object.
 * @param wire_status Receives the check result.
 * @return ANALOG_INPUT_OK, ANALOG_INPUT_ERR_ARG for a NULL pointer, or
 *         ANALOG_INPUT_ERR_STATE when the object has no operation table.
 * @note Meaningful once the delay of analog_input_enable_break_detection() has
 *       elapsed. The function reads the converter once and does not wait, and
 *       an implementation that can tell the faults apart reports the specific
 *       value instead of ANALOG_INPUT_WIRE_BROKEN.
 */
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
