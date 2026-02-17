#ifndef APU_H
#define APU_H

#include <stdint.h>
#include <stdbool.h>

/* Game Boy APU (Audio Processing Unit) - 4 sound channels */

#define APU_SAMPLE_RATE   48000
#define APU_BUFFER_SIZE   2048
#define APU_CPU_FREQ      4194304

typedef struct gb_state gb_state_t;

/* Channel 1: Pulse with sweep */
typedef struct {
    bool enabled;
    bool dac_enabled;
    uint16_t length_counter;
    bool length_enabled;
    uint16_t frequency;
    int32_t timer;
    uint8_t output_volume;

    /* Sweep */
    uint8_t sweep_period;
    uint8_t sweep_direction;    /* 0 = add, 1 = subtract */
    uint8_t sweep_shift;
    uint8_t sweep_timer;
    uint16_t shadow_freq;
    bool sweep_enabled;

    /* Envelope */
    uint8_t envelope_period;
    uint8_t envelope_direction; /* 0 = decrease, 1 = increase */
    uint8_t envelope_timer;
    uint8_t volume;

    /* Duty */
    uint8_t duty;               /* 0-3 */
    uint8_t duty_pos;           /* 0-7 */
} apu_channel1_t;

/* Channel 2: Pulse (no sweep) */
typedef struct {
    bool enabled;
    bool dac_enabled;
    uint16_t length_counter;
    bool length_enabled;
    uint16_t frequency;
    int32_t timer;
    uint8_t output_volume;

    /* Envelope */
    uint8_t envelope_period;
    uint8_t envelope_direction;
    uint8_t envelope_timer;
    uint8_t volume;

    /* Duty */
    uint8_t duty;
    uint8_t duty_pos;
} apu_channel2_t;

/* Channel 3: Wave */
typedef struct {
    bool enabled;
    bool dac_enabled;
    uint16_t length_counter;
    bool length_enabled;
    uint16_t frequency;
    int32_t timer;
    uint8_t output_volume;

    uint8_t wave_ram[16];       /* 32 4-bit samples packed into 16 bytes */
    uint8_t volume_code;        /* 0-3: mute, 100%, 50%, 25% */
    uint8_t position;           /* 0-31 sample position */
} apu_channel3_t;

/* Channel 4: Noise */
typedef struct {
    bool enabled;
    bool dac_enabled;
    uint16_t length_counter;
    bool length_enabled;
    uint16_t frequency;         /* not used directly, kept for consistency */
    int32_t timer;
    uint8_t output_volume;

    uint16_t lfsr;              /* 15-bit linear feedback shift register */
    uint8_t clock_shift;
    uint8_t width_mode;         /* 0 = 15-bit, 1 = 7-bit */
    uint8_t divisor_code;

    /* Envelope */
    uint8_t envelope_period;
    uint8_t envelope_direction;
    uint8_t envelope_timer;
    uint8_t volume;
} apu_channel4_t;

/* APU state */
typedef struct apu_state {
    apu_channel1_t ch1;
    apu_channel2_t ch2;
    apu_channel3_t ch3;
    apu_channel4_t ch4;

    uint8_t nr50;               /* Master volume / VIN panning */
    uint8_t nr51;               /* Sound panning */
    uint8_t nr52;               /* Sound on/off */

    /* Frame sequencer (512 Hz = every 8192 T-cycles) */
    uint32_t frame_seq_counter;
    uint8_t frame_seq_step;

    /* Sample generation */
    float sample_buffer[APU_BUFFER_SIZE * 2]; /* interleaved stereo L,R */
    int sample_count;
    uint32_t sample_counter;
    uint32_t cycles_per_sample; /* APU_CPU_FREQ / APU_SAMPLE_RATE */

    bool enabled;
} apu_state_t;

/* Initialize APU to power-on state */
void apu_init(apu_state_t *apu);

/* Advance APU by the given number of T-cycles */
void apu_tick(apu_state_t *apu, gb_state_t *gb, uint32_t cycles);

/* Read an NRxx register (reg = IO offset 0x10-0x26) */
uint8_t apu_read_reg(apu_state_t *apu, uint8_t reg);

/* Write an NRxx register (reg = IO offset 0x10-0x26) */
void apu_write_reg(apu_state_t *apu, uint8_t reg, uint8_t val);

/* Read wave RAM (index 0x00-0x0F) */
uint8_t apu_read_wave(apu_state_t *apu, uint8_t index);

/* Write wave RAM (index 0x00-0x0F) */
void apu_write_wave(apu_state_t *apu, uint8_t index, uint8_t val);

/* Copy accumulated samples into buf (interleaved stereo). Returns samples copied. */
int apu_get_samples(apu_state_t *apu, float *buf, int count);

#endif /* APU_H */
