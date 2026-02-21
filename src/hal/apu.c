#include "apu.h"
#include "cpu.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

#define FRAME_SEQ_PERIOD 8192  /* T-cycles per frame sequencer tick (512 Hz) */

/* High-pass filter charge factor per output sample.
 * The Game Boy has an RC high-pass on the audio DAC output.
 * At 48 kHz sample rate the per-sample factor is ~0.999958,
 * derived from the hardware's ~7 ms time constant. */
#define HPF_CHARGE_FACTOR 0.999958f

/* Duty waveforms: 1 = high, 0 = low.  Index by [duty][position]. */
static const uint8_t duty_table[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },  /* 12.5% */
    { 1, 0, 0, 0, 0, 0, 0, 1 },  /* 25%   */
    { 1, 0, 0, 0, 0, 1, 1, 1 },  /* 50%   */
    { 0, 1, 1, 1, 1, 1, 1, 0 },  /* 75%   */
};

/* Noise channel divisor table */
static const uint16_t divisor_table[8] = {
    8, 16, 32, 48, 64, 80, 96, 112
};

/* OR-masks for reading NRxx registers (indexed by IO offset 0x10-0x26) */
static const uint8_t read_masks[0x17] = {
    /* 0x10 NR10 */ 0x80,
    /* 0x11 NR11 */ 0x3F,
    /* 0x12 NR12 */ 0x00,
    /* 0x13 NR13 */ 0xFF,
    /* 0x14 NR14 */ 0xBF,
    /* 0x15 ---- */ 0xFF,
    /* 0x16 NR21 */ 0x3F,
    /* 0x17 NR22 */ 0x00,
    /* 0x18 NR23 */ 0xFF,
    /* 0x19 NR24 */ 0xBF,
    /* 0x1A NR30 */ 0x7F,
    /* 0x1B NR31 */ 0xFF,
    /* 0x1C NR32 */ 0x9F,
    /* 0x1D NR33 */ 0xFF,
    /* 0x1E NR34 */ 0xBF,
    /* 0x1F ---- */ 0xFF,
    /* 0x20 NR41 */ 0xFF,
    /* 0x21 NR42 */ 0x00,
    /* 0x22 NR43 */ 0x00,
    /* 0x23 NR44 */ 0xBF,
    /* 0x24 NR50 */ 0x00,
    /* 0x25 NR51 */ 0x00,
    /* 0x26 NR52 */ 0x70,
};

/* ------------------------------------------------------------------ */
/*  Forward declarations (internal helpers)                           */
/* ------------------------------------------------------------------ */

static void tick_length(apu_state_t *apu);
static void tick_sweep(apu_state_t *apu);
static void tick_envelope(apu_state_t *apu);
static void tick_channel_timers(apu_state_t *apu);
static float mix_sample(apu_state_t *apu, bool right);
static uint16_t sweep_calculate(apu_state_t *apu);
static void sweep_overflow_check(apu_state_t *apu, uint16_t new_freq);
static void trigger_ch1(apu_state_t *apu);
static void trigger_ch2(apu_state_t *apu);
static void trigger_ch3(apu_state_t *apu);
static void trigger_ch4(apu_state_t *apu);
static void power_off(apu_state_t *apu);

/* ------------------------------------------------------------------ */
/*  Init                                                              */
/* ------------------------------------------------------------------ */

void apu_init(apu_state_t *apu)
{
    SDL_mutex *saved_lock = apu->lock; /* preserve if re-init */
    memset(apu, 0, sizeof(*apu));
    apu->sample_accum = 0;
    apu->enabled = false;
    apu->ch4.lfsr = 0x7FFF;
    apu->lock = saved_lock ? saved_lock : SDL_CreateMutex();
}

/* ------------------------------------------------------------------ */
/*  Frame sequencer clocks                                            */
/* ------------------------------------------------------------------ */

static void tick_length(apu_state_t *apu)
{
    /* Channel 1 */
    if (apu->ch1.length_enabled && apu->ch1.length_counter > 0) {
        apu->ch1.length_counter--;
        if (apu->ch1.length_counter == 0)
            apu->ch1.enabled = false;
    }
    /* Channel 2 */
    if (apu->ch2.length_enabled && apu->ch2.length_counter > 0) {
        apu->ch2.length_counter--;
        if (apu->ch2.length_counter == 0)
            apu->ch2.enabled = false;
    }
    /* Channel 3 */
    if (apu->ch3.length_enabled && apu->ch3.length_counter > 0) {
        apu->ch3.length_counter--;
        if (apu->ch3.length_counter == 0)
            apu->ch3.enabled = false;
    }
    /* Channel 4 */
    if (apu->ch4.length_enabled && apu->ch4.length_counter > 0) {
        apu->ch4.length_counter--;
        if (apu->ch4.length_counter == 0)
            apu->ch4.enabled = false;
    }
}

static uint16_t sweep_calculate(apu_state_t *apu)
{
    uint16_t shifted = apu->ch1.shadow_freq >> apu->ch1.sweep_shift;
    uint16_t new_freq;
    if (apu->ch1.sweep_direction) {
        new_freq = apu->ch1.shadow_freq - shifted;
        apu->ch1.sweep_negate_used = true;
    } else {
        new_freq = apu->ch1.shadow_freq + shifted;
    }
    return new_freq;
}

static void sweep_overflow_check(apu_state_t *apu, uint16_t new_freq)
{
    if (new_freq > 2047)
        apu->ch1.enabled = false;
}

static void tick_sweep(apu_state_t *apu)
{
    if (apu->ch1.sweep_timer > 0)
        apu->ch1.sweep_timer--;

    if (apu->ch1.sweep_timer == 0) {
        /* Reload timer (period of 0 is treated as 8) */
        apu->ch1.sweep_timer = apu->ch1.sweep_period ? apu->ch1.sweep_period : 8;

        if (apu->ch1.sweep_enabled && apu->ch1.sweep_period > 0) {
            uint16_t new_freq = sweep_calculate(apu);
            sweep_overflow_check(apu, new_freq);

            if (new_freq <= 2047 && apu->ch1.sweep_shift > 0) {
                apu->ch1.shadow_freq = new_freq;
                apu->ch1.frequency = new_freq;

                /* Overflow check again with the new frequency */
                new_freq = sweep_calculate(apu);
                sweep_overflow_check(apu, new_freq);
            }
        }
    }
}

static void tick_envelope_channel(uint8_t *volume, uint8_t *timer,
                                  uint8_t period, uint8_t direction)
{
    if (period == 0) return;

    if (*timer > 0)
        (*timer)--;

    if (*timer == 0) {
        *timer = period;

        if (direction && *volume < 15)
            (*volume)++;
        else if (!direction && *volume > 0)
            (*volume)--;
    }
}

static void tick_envelope(apu_state_t *apu)
{
    tick_envelope_channel(&apu->ch1.volume, &apu->ch1.envelope_timer,
                          apu->ch1.envelope_period, apu->ch1.envelope_direction);
    tick_envelope_channel(&apu->ch2.volume, &apu->ch2.envelope_timer,
                          apu->ch2.envelope_period, apu->ch2.envelope_direction);
    tick_envelope_channel(&apu->ch4.volume, &apu->ch4.envelope_timer,
                          apu->ch4.envelope_period, apu->ch4.envelope_direction);
}

/* ------------------------------------------------------------------ */
/*  Channel timer ticking (per T-cycle)                               */
/* ------------------------------------------------------------------ */

static void tick_channel_timers(apu_state_t *apu)
{
    /* Channel 1 - Pulse */
    apu->ch1.timer--;
    if (apu->ch1.timer <= 0) {
        apu->ch1.timer = (2048 - apu->ch1.frequency) * 4;
        apu->ch1.duty_pos = (apu->ch1.duty_pos + 1) & 7;
    }

    /* Channel 2 - Pulse */
    apu->ch2.timer--;
    if (apu->ch2.timer <= 0) {
        apu->ch2.timer = (2048 - apu->ch2.frequency) * 4;
        apu->ch2.duty_pos = (apu->ch2.duty_pos + 1) & 7;
    }

    /* Channel 3 - Wave */
    apu->ch3.timer--;
    if (apu->ch3.timer <= 0) {
        apu->ch3.timer = (2048 - apu->ch3.frequency) * 2;
        apu->ch3.position = (apu->ch3.position + 1) & 31;
    }

    /* Channel 4 - Noise */
    apu->ch4.timer--;
    if (apu->ch4.timer <= 0) {
        apu->ch4.timer = (int32_t)(divisor_table[apu->ch4.divisor_code] << apu->ch4.clock_shift);
        if (apu->ch4.timer == 0)
            apu->ch4.timer = 8; /* divisor 0 with shift 0 special case */

        /* Clock the LFSR */
        uint8_t xor_bit = (uint8_t)((apu->ch4.lfsr & 1) ^ ((apu->ch4.lfsr >> 1) & 1));
        apu->ch4.lfsr >>= 1;
        apu->ch4.lfsr |= (uint16_t)(xor_bit << 14);

        if (apu->ch4.width_mode) {
            /* 7-bit mode: also set bit 6 */
            apu->ch4.lfsr &= ~(1u << 6);
            apu->ch4.lfsr |= (uint16_t)(xor_bit << 6);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Sample mixing                                                     */
/* ------------------------------------------------------------------ */

static uint8_t get_ch1_output(const apu_state_t *apu)
{
    if (!apu->ch1.enabled || !apu->ch1.dac_enabled) return 0;
    return duty_table[apu->ch1.duty][apu->ch1.duty_pos] ? apu->ch1.volume : 0;
}

static uint8_t get_ch2_output(const apu_state_t *apu)
{
    if (!apu->ch2.enabled || !apu->ch2.dac_enabled) return 0;
    return duty_table[apu->ch2.duty][apu->ch2.duty_pos] ? apu->ch2.volume : 0;
}

static uint8_t get_ch3_output(const apu_state_t *apu)
{
    if (!apu->ch3.enabled || !apu->ch3.dac_enabled) return 0;

    /* Get current 4-bit sample from wave RAM */
    uint8_t byte = apu->ch3.wave_ram[apu->ch3.position / 2];
    uint8_t sample;
    if ((apu->ch3.position & 1) == 0)
        sample = (byte >> 4) & 0x0F;   /* high nibble first */
    else
        sample = byte & 0x0F;          /* low nibble second */

    /* Apply volume shift: 0=mute, 1=100%, 2=50%, 3=25% */
    static const uint8_t shift_table[4] = { 4, 0, 1, 2 };
    sample >>= shift_table[apu->ch3.volume_code];

    return sample;
}

static uint8_t get_ch4_output(const apu_state_t *apu)
{
    if (!apu->ch4.enabled || !apu->ch4.dac_enabled) return 0;
    /* Output is inverted bit 0 of LFSR */
    return (~apu->ch4.lfsr & 1) ? apu->ch4.volume : 0;
}

/* Convert a channel's digital output (0-15) to a DAC-centered float.
 * Returns 0.0f when the channel is disconnected (DAC off / disabled),
 * and a value in [-1.0, +1.0] when the DAC is active. */
static float dac_convert(bool enabled, bool dac_on, uint8_t digital)
{
    if (!enabled || !dac_on) return 0.0f;
    return ((float)digital / 7.5f) - 1.0f;
}

static float mix_sample(apu_state_t *apu, bool right)
{
    float sum = 0.0f;
    uint8_t panning = apu->nr51;

    /* Get DAC-centered outputs: [-1,+1] when active, 0 when off */
    float ch1 = dac_convert(apu->ch1.enabled, apu->ch1.dac_enabled,
                            duty_table[apu->ch1.duty][apu->ch1.duty_pos] ? apu->ch1.volume : 0);
    float ch2 = dac_convert(apu->ch2.enabled, apu->ch2.dac_enabled,
                            duty_table[apu->ch2.duty][apu->ch2.duty_pos] ? apu->ch2.volume : 0);
    float ch3 = dac_convert(apu->ch3.enabled, apu->ch3.dac_enabled,
                            get_ch3_output(apu));
    float ch4 = dac_convert(apu->ch4.enabled, apu->ch4.dac_enabled,
                            (~apu->ch4.lfsr & 1) ? apu->ch4.volume : 0);

    if (right) {
        /* Right channel: bits 0-3 of NR51 */
        if (panning & 0x01) sum += ch1;
        if (panning & 0x02) sum += ch2;
        if (panning & 0x04) sum += ch3;
        if (panning & 0x08) sum += ch4;
    } else {
        /* Left channel: bits 4-7 of NR51 */
        if (panning & 0x10) sum += ch1;
        if (panning & 0x20) sum += ch2;
        if (panning & 0x40) sum += ch3;
        if (panning & 0x80) sum += ch4;
    }

    /* Apply master volume (1-8 from NR50) and normalise.
     * Each channel is in [-1,+1]; 4 channels * volume 8 = 32 max.
     * Divide by 32 to keep result in [-1,+1]. */
    uint8_t master_vol = right ? (apu->nr50 & 0x07) : ((apu->nr50 >> 4) & 0x07);
    sum *= (float)(master_vol + 1) / 32.0f;

    return sum;
}

/* ------------------------------------------------------------------ */
/*  Trigger events                                                    */
/* ------------------------------------------------------------------ */

static void trigger_ch1(apu_state_t *apu)
{
    apu->ch1.enabled = true;
    if (apu->ch1.length_counter == 0) {
        apu->ch1.length_counter = 64;
        /* Length glitch: if length is being enabled on a step that clocks
         * length (next step is even), the fresh counter is clocked immediately */
        if (apu->ch1.length_enabled && (apu->frame_seq_step & 1) == 0)
            apu->ch1.length_counter--;
    }

    /* Reload frequency timer */
    apu->ch1.timer = (2048 - apu->ch1.frequency) * 4;

    /* Reload envelope */
    apu->ch1.envelope_timer = apu->ch1.envelope_period;
    apu->ch1.volume = apu->ch1.output_volume;

    /* Sweep init */
    apu->ch1.shadow_freq = apu->ch1.frequency;
    apu->ch1.sweep_timer = apu->ch1.sweep_period ? apu->ch1.sweep_period : 8;
    apu->ch1.sweep_enabled = (apu->ch1.sweep_period > 0 || apu->ch1.sweep_shift > 0);
    apu->ch1.sweep_negate_used = false;

    /* If shift is non-zero, do overflow check immediately */
    if (apu->ch1.sweep_shift > 0) {
        uint16_t new_freq = sweep_calculate(apu);
        sweep_overflow_check(apu, new_freq);
    }

    /* DAC check */
    if (!apu->ch1.dac_enabled)
        apu->ch1.enabled = false;
}

static void trigger_ch2(apu_state_t *apu)
{
    apu->ch2.enabled = true;
    if (apu->ch2.length_counter == 0) {
        apu->ch2.length_counter = 64;
        if (apu->ch2.length_enabled && (apu->frame_seq_step & 1) == 0)
            apu->ch2.length_counter--;
    }

    apu->ch2.timer = (2048 - apu->ch2.frequency) * 4;

    apu->ch2.envelope_timer = apu->ch2.envelope_period;
    apu->ch2.volume = apu->ch2.output_volume;

    if (!apu->ch2.dac_enabled)
        apu->ch2.enabled = false;
}

static void trigger_ch3(apu_state_t *apu)
{
    apu->ch3.enabled = true;
    if (apu->ch3.length_counter == 0) {
        apu->ch3.length_counter = 256;
        if (apu->ch3.length_enabled && (apu->frame_seq_step & 1) == 0)
            apu->ch3.length_counter--;
    }

    apu->ch3.timer = (2048 - apu->ch3.frequency) * 2;
    apu->ch3.position = 0;

    if (!apu->ch3.dac_enabled)
        apu->ch3.enabled = false;
}

static void trigger_ch4(apu_state_t *apu)
{
    apu->ch4.enabled = true;
    if (apu->ch4.length_counter == 0) {
        apu->ch4.length_counter = 64;
        if (apu->ch4.length_enabled && (apu->frame_seq_step & 1) == 0)
            apu->ch4.length_counter--;
    }

    apu->ch4.timer = (int32_t)(divisor_table[apu->ch4.divisor_code] << apu->ch4.clock_shift);
    if (apu->ch4.timer == 0)
        apu->ch4.timer = 8;

    apu->ch4.envelope_timer = apu->ch4.envelope_period;
    apu->ch4.volume = apu->ch4.output_volume;

    apu->ch4.lfsr = 0x7FFF;

    if (!apu->ch4.dac_enabled)
        apu->ch4.enabled = false;
}

/* ------------------------------------------------------------------ */
/*  Power off/on                                                      */
/* ------------------------------------------------------------------ */

static void power_off(apu_state_t *apu)
{
    /* On DMG, length counters are preserved across power off.
     * All other channel state and registers are zeroed. */
    uint16_t len1 = apu->ch1.length_counter;
    uint16_t len2 = apu->ch2.length_counter;
    uint16_t len3 = apu->ch3.length_counter;
    uint16_t len4 = apu->ch4.length_counter;

    memset(&apu->ch1, 0, sizeof(apu->ch1));
    memset(&apu->ch2, 0, sizeof(apu->ch2));

    /* Preserve wave RAM for ch3 */
    uint8_t wave_backup[16];
    memcpy(wave_backup, apu->ch3.wave_ram, 16);
    memset(&apu->ch3, 0, sizeof(apu->ch3));
    memcpy(apu->ch3.wave_ram, wave_backup, 16);

    memset(&apu->ch4, 0, sizeof(apu->ch4));
    apu->ch4.lfsr = 0x7FFF;

    /* Restore length counters */
    apu->ch1.length_counter = len1;
    apu->ch2.length_counter = len2;
    apu->ch3.length_counter = len3;
    apu->ch4.length_counter = len4;

    apu->nr50 = 0;
    apu->nr51 = 0;
    apu->enabled = false;
}

/* ------------------------------------------------------------------ */
/*  Register write                                                    */
/* ------------------------------------------------------------------ */

void apu_write_reg(apu_state_t *apu, uint8_t reg, uint8_t val)
{
    /* NR52 is always writable */
    if (reg == 0x26) {
        bool was_enabled = apu->enabled;
        apu->enabled = (val & 0x80) != 0;
        if (was_enabled && !apu->enabled)
            power_off(apu);
        if (!was_enabled && apu->enabled)
            apu->frame_seq_step = 0;
        return;
    }

    /* When APU is off, only NR52 and length counters (NRx1) are writable */
    if (!apu->enabled) {
        /* Allow writes to length counters on DMG */
        switch (reg) {
        case 0x11: apu->ch1.length_counter = 64  - (val & 0x3F); return;
        case 0x16: apu->ch2.length_counter = 64  - (val & 0x3F); return;
        case 0x1B: apu->ch3.length_counter = 256 - val;          return;
        case 0x20: apu->ch4.length_counter = 64  - (val & 0x3F); return;
        default: return;
        }
    }

    switch (reg) {
    /* ---- Channel 1 (NR10-NR14) ---- */
    case 0x10: { /* NR10 - Sweep */
        uint8_t old_direction = apu->ch1.sweep_direction;
        apu->ch1.sweep_period    = (val >> 4) & 0x07;
        apu->ch1.sweep_direction = (val >> 3) & 0x01;
        apu->ch1.sweep_shift     = val & 0x07;
        /* Quirk: switching from negate to add without re-trigger disables Ch1 */
        if (old_direction && !apu->ch1.sweep_direction && apu->ch1.sweep_negate_used)
            apu->ch1.enabled = false;
        break;
    }

    case 0x11: /* NR11 - Duty / Length */
        apu->ch1.duty           = (val >> 6) & 0x03;
        apu->ch1.length_counter = 64 - (val & 0x3F);
        break;

    case 0x12: /* NR12 - Envelope */
        apu->ch1.output_volume      = (val >> 4) & 0x0F;
        apu->ch1.envelope_direction = (val >> 3) & 0x01;
        apu->ch1.envelope_period    = val & 0x07;
        /* DAC is enabled if upper 5 bits are non-zero */
        apu->ch1.dac_enabled = (val & 0xF8) != 0;
        if (!apu->ch1.dac_enabled)
            apu->ch1.enabled = false;
        break;

    case 0x13: /* NR13 - Frequency low */
        apu->ch1.frequency = (apu->ch1.frequency & 0x700) | val;
        break;

    case 0x14: { /* NR14 - Trigger / Length enable / Frequency high */
        apu->ch1.frequency = (apu->ch1.frequency & 0x00FF) | (uint16_t)((val & 0x07) << 8);
        bool was_len = apu->ch1.length_enabled;
        apu->ch1.length_enabled = (val & 0x40) != 0;
        /* Extra length clock: enabling length on a length-clocking step */
        if (!was_len && apu->ch1.length_enabled && (apu->frame_seq_step & 1) == 0 &&
            apu->ch1.length_counter > 0) {
            apu->ch1.length_counter--;
            if (apu->ch1.length_counter == 0 && !(val & 0x80))
                apu->ch1.enabled = false;
        }
        if (val & 0x80)
            trigger_ch1(apu);
        break;
    }

    /* ---- Channel 2 (NR21-NR24) ---- */
    case 0x16: /* NR21 - Duty / Length */
        apu->ch2.duty           = (val >> 6) & 0x03;
        apu->ch2.length_counter = 64 - (val & 0x3F);
        break;

    case 0x17: /* NR22 - Envelope */
        apu->ch2.output_volume      = (val >> 4) & 0x0F;
        apu->ch2.envelope_direction = (val >> 3) & 0x01;
        apu->ch2.envelope_period    = val & 0x07;
        apu->ch2.dac_enabled = (val & 0xF8) != 0;
        if (!apu->ch2.dac_enabled)
            apu->ch2.enabled = false;
        break;

    case 0x18: /* NR23 - Frequency low */
        apu->ch2.frequency = (apu->ch2.frequency & 0x700) | val;
        break;

    case 0x19: { /* NR24 - Trigger / Length enable / Frequency high */
        apu->ch2.frequency = (apu->ch2.frequency & 0x00FF) | (uint16_t)((val & 0x07) << 8);
        bool was_len = apu->ch2.length_enabled;
        apu->ch2.length_enabled = (val & 0x40) != 0;
        if (!was_len && apu->ch2.length_enabled && (apu->frame_seq_step & 1) == 0 &&
            apu->ch2.length_counter > 0) {
            apu->ch2.length_counter--;
            if (apu->ch2.length_counter == 0 && !(val & 0x80))
                apu->ch2.enabled = false;
        }
        if (val & 0x80)
            trigger_ch2(apu);
        break;
    }

    /* ---- Channel 3 (NR30-NR34) ---- */
    case 0x1A: /* NR30 - DAC enable */
        apu->ch3.dac_enabled = (val & 0x80) != 0;
        if (!apu->ch3.dac_enabled)
            apu->ch3.enabled = false;
        break;

    case 0x1B: /* NR31 - Length */
        apu->ch3.length_counter = 256 - val;
        break;

    case 0x1C: /* NR32 - Volume code */
        apu->ch3.volume_code = (val >> 5) & 0x03;
        break;

    case 0x1D: /* NR33 - Frequency low */
        apu->ch3.frequency = (apu->ch3.frequency & 0x700) | val;
        break;

    case 0x1E: { /* NR34 - Trigger / Length enable / Frequency high */
        apu->ch3.frequency = (apu->ch3.frequency & 0x00FF) | (uint16_t)((val & 0x07) << 8);
        bool was_len = apu->ch3.length_enabled;
        apu->ch3.length_enabled = (val & 0x40) != 0;
        if (!was_len && apu->ch3.length_enabled && (apu->frame_seq_step & 1) == 0 &&
            apu->ch3.length_counter > 0) {
            apu->ch3.length_counter--;
            if (apu->ch3.length_counter == 0 && !(val & 0x80))
                apu->ch3.enabled = false;
        }
        if (val & 0x80)
            trigger_ch3(apu);
        break;
    }

    /* ---- Channel 4 (NR41-NR44) ---- */
    case 0x20: /* NR41 - Length */
        apu->ch4.length_counter = 64 - (val & 0x3F);
        break;

    case 0x21: /* NR42 - Envelope */
        apu->ch4.output_volume      = (val >> 4) & 0x0F;
        apu->ch4.envelope_direction = (val >> 3) & 0x01;
        apu->ch4.envelope_period    = val & 0x07;
        apu->ch4.dac_enabled = (val & 0xF8) != 0;
        if (!apu->ch4.dac_enabled)
            apu->ch4.enabled = false;
        break;

    case 0x22: /* NR43 - Polynomial counter */
        apu->ch4.clock_shift  = (val >> 4) & 0x0F;
        apu->ch4.width_mode   = (val >> 3) & 0x01;
        apu->ch4.divisor_code = val & 0x07;
        break;

    case 0x23: { /* NR44 - Trigger / Length enable */
        bool was_len = apu->ch4.length_enabled;
        apu->ch4.length_enabled = (val & 0x40) != 0;
        if (!was_len && apu->ch4.length_enabled && (apu->frame_seq_step & 1) == 0 &&
            apu->ch4.length_counter > 0) {
            apu->ch4.length_counter--;
            if (apu->ch4.length_counter == 0 && !(val & 0x80))
                apu->ch4.enabled = false;
        }
        if (val & 0x80)
            trigger_ch4(apu);
        break;
    }

    /* ---- Master control (NR50-NR52 handled above for NR52) ---- */
    case 0x24: /* NR50 - Master volume */
        apu->nr50 = val;
        break;

    case 0x25: /* NR51 - Panning */
        apu->nr51 = val;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Register read                                                     */
/* ------------------------------------------------------------------ */

uint8_t apu_read_reg(apu_state_t *apu, uint8_t reg)
{
    uint8_t val = 0xFF; /* default: all bits high for unmapped */

    if (reg < 0x10 || reg > 0x26)
        return 0xFF;

    /* NR52 is special: returns channel status in bits 0-3 */
    if (reg == 0x26) {
        val = 0x70; /* bits 4-6 always read 1 */
        if (apu->enabled)     val |= 0x80;
        if (apu->ch1.enabled) val |= 0x01;
        if (apu->ch2.enabled) val |= 0x02;
        if (apu->ch3.enabled) val |= 0x04;
        if (apu->ch4.enabled) val |= 0x08;
        return val;
    }

    switch (reg) {
    /* Channel 1 */
    case 0x10:
        val = (uint8_t)(0x80 |
              (apu->ch1.sweep_period << 4) |
              (apu->ch1.sweep_direction << 3) |
              apu->ch1.sweep_shift);
        break;
    case 0x11:
        val = (uint8_t)(0x3F | (apu->ch1.duty << 6));
        break;
    case 0x12:
        val = (uint8_t)((apu->ch1.output_volume << 4) |
              (apu->ch1.envelope_direction << 3) |
              apu->ch1.envelope_period);
        break;
    case 0x13:
        val = 0xFF; /* write-only */
        break;
    case 0x14:
        val = (uint8_t)(0xBF | (apu->ch1.length_enabled ? 0x40 : 0));
        break;

    /* Channel 2 */
    case 0x16:
        val = (uint8_t)(0x3F | (apu->ch2.duty << 6));
        break;
    case 0x17:
        val = (uint8_t)((apu->ch2.output_volume << 4) |
              (apu->ch2.envelope_direction << 3) |
              apu->ch2.envelope_period);
        break;
    case 0x18:
        val = 0xFF; /* write-only */
        break;
    case 0x19:
        val = (uint8_t)(0xBF | (apu->ch2.length_enabled ? 0x40 : 0));
        break;

    /* Channel 3 */
    case 0x1A:
        val = (uint8_t)(0x7F | (apu->ch3.dac_enabled ? 0x80 : 0));
        break;
    case 0x1B:
        val = 0xFF; /* write-only */
        break;
    case 0x1C:
        val = (uint8_t)(0x9F | (apu->ch3.volume_code << 5));
        break;
    case 0x1D:
        val = 0xFF; /* write-only */
        break;
    case 0x1E:
        val = (uint8_t)(0xBF | (apu->ch3.length_enabled ? 0x40 : 0));
        break;

    /* Channel 4 */
    case 0x20:
        val = 0xFF; /* write-only */
        break;
    case 0x21:
        val = (uint8_t)((apu->ch4.output_volume << 4) |
              (apu->ch4.envelope_direction << 3) |
              apu->ch4.envelope_period);
        break;
    case 0x22:
        val = (uint8_t)((apu->ch4.clock_shift << 4) |
              (apu->ch4.width_mode << 3) |
              apu->ch4.divisor_code);
        break;
    case 0x23:
        val = (uint8_t)(0xBF | (apu->ch4.length_enabled ? 0x40 : 0));
        break;

    /* Master */
    case 0x24:
        val = apu->nr50;
        break;
    case 0x25:
        val = apu->nr51;
        break;

    default:
        /* Unmapped registers between channels (0x15, 0x1F) */
        val = 0xFF;
        break;
    }

    return val;
}

/* ------------------------------------------------------------------ */
/*  Wave RAM access                                                   */
/* ------------------------------------------------------------------ */

uint8_t apu_read_wave(apu_state_t *apu, uint8_t index)
{
    if (index >= 16) return 0xFF;

    /* If ch3 is actively reading wave RAM, return the byte it is reading.
       For simplicity we allow reads at any time. */
    return apu->ch3.wave_ram[index];
}

void apu_write_wave(apu_state_t *apu, uint8_t index, uint8_t val)
{
    if (index >= 16) return;
    apu->ch3.wave_ram[index] = val;
}

/* ------------------------------------------------------------------ */
/*  Main tick                                                         */
/* ------------------------------------------------------------------ */

void apu_destroy(apu_state_t *apu)
{
    if (apu->lock) {
        SDL_DestroyMutex(apu->lock);
        apu->lock = NULL;
    }
}

void apu_tick(apu_state_t *apu, gb_state_t *gb, uint32_t cycles)
{
    (void)gb;

    if (!apu->enabled) {
        /* Fast path: compute silence samples without per-cycle loop.
         * Each T-cycle adds APU_SAMPLE_RATE to the accumulator; a sample
         * is emitted every time it crosses APU_CPU_FREQ. */
        uint64_t total = (uint64_t)apu->sample_accum + (uint64_t)cycles * APU_SAMPLE_RATE;
        uint32_t samples = (uint32_t)(total / APU_CPU_FREQ);
        apu->sample_accum = (uint32_t)(total % APU_CPU_FREQ);
        if (samples > 0) {
            /* Run HPF on silence to decay capacitor (prevents pop on re-enable) */
            float local_buf[128 * 2];
            uint32_t remaining = samples;
            SDL_LockMutex(apu->lock);
            while (remaining > 0) {
                uint32_t batch = remaining < 128 ? remaining : 128;
                for (uint32_t j = 0; j < batch; j++) {
                    float out_l = 0.0f - apu->hpf_capacitor_l;
                    apu->hpf_capacitor_l = out_l * HPF_CHARGE_FACTOR;
                    float out_r = 0.0f - apu->hpf_capacitor_r;
                    apu->hpf_capacitor_r = out_r * HPF_CHARGE_FACTOR;
                    local_buf[j * 2]     = out_l;
                    local_buf[j * 2 + 1] = out_r;
                }
                int avail = APU_BUFFER_SIZE - apu->sample_count;
                int to_copy = (int)batch < avail ? (int)batch : avail;
                if (to_copy > 0) {
                    memcpy(&apu->sample_buffer[apu->sample_count * 2],
                           local_buf, (size_t)(to_copy * 2) * sizeof(float));
                    apu->sample_count += to_copy;
                }
                remaining -= batch;
            }
            SDL_UnlockMutex(apu->lock);
        }
        return;
    }

    /* Batch sample writes: collect into a local buffer first, then
     * copy under the lock in one shot to reduce lock contention. */
    float local_buf[128 * 2]; /* enough for ~2.6ms of audio at 48kHz */
    int local_count = 0;

    for (uint32_t i = 0; i < cycles; i++) {
        /* Tick channel frequency timers every T-cycle */
        tick_channel_timers(apu);

        /* Frame sequencer: 512 Hz = every 8192 T-cycles */
        apu->frame_seq_counter++;
        if (apu->frame_seq_counter >= FRAME_SEQ_PERIOD) {
            apu->frame_seq_counter = 0;

            switch (apu->frame_seq_step) {
            case 0: tick_length(apu);                       break;
            case 1:                                         break;
            case 2: tick_length(apu); tick_sweep(apu);      break;
            case 3:                                         break;
            case 4: tick_length(apu);                       break;
            case 5:                                         break;
            case 6: tick_length(apu); tick_sweep(apu);      break;
            case 7: tick_envelope(apu);                     break;
            }
            apu->frame_seq_step = (apu->frame_seq_step + 1) & 7;
        }

        /* Sample accumulation: Bresenham for exact APU_SAMPLE_RATE */
        apu->sample_accum += APU_SAMPLE_RATE;
        if (apu->sample_accum >= APU_CPU_FREQ) {
            apu->sample_accum -= APU_CPU_FREQ;

            if (local_count < 128) {
                int idx = local_count * 2;
                float raw_l = mix_sample(apu, false);
                float raw_r = mix_sample(apu, true);
                /* High-pass filter (DC-blocking capacitor) */
                float out_l = raw_l - apu->hpf_capacitor_l;
                apu->hpf_capacitor_l = out_l * HPF_CHARGE_FACTOR;
                float out_r = raw_r - apu->hpf_capacitor_r;
                apu->hpf_capacitor_r = out_r * HPF_CHARGE_FACTOR;
                local_buf[idx]     = out_l;
                local_buf[idx + 1] = out_r;
                local_count++;
            }
        }
    }

    /* Flush local buffer under lock */
    if (local_count > 0) {
        SDL_LockMutex(apu->lock);
        int avail = APU_BUFFER_SIZE - apu->sample_count;
        int to_copy = local_count < avail ? local_count : avail;
        if (to_copy > 0) {
            memcpy(&apu->sample_buffer[apu->sample_count * 2],
                   local_buf, (size_t)(to_copy * 2) * sizeof(float));
            apu->sample_count += to_copy;
        }
        SDL_UnlockMutex(apu->lock);
    }
}

/* ------------------------------------------------------------------ */
/*  Get accumulated samples for the audio callback                    */
/* ------------------------------------------------------------------ */

int apu_get_samples(apu_state_t *apu, float *buf, int count)
{
    SDL_LockMutex(apu->lock);

    /* count is in stereo frames; each frame is 2 floats (L, R) */
    int available = apu->sample_count;
    if (count > available)
        count = available;

    if (count > 0) {
        memcpy(buf, apu->sample_buffer, (size_t)(count * 2) * sizeof(float));

        /* Shift remaining samples forward */
        int remaining = available - count;
        if (remaining > 0) {
            memmove(apu->sample_buffer,
                    apu->sample_buffer + count * 2,
                    (size_t)(remaining * 2) * sizeof(float));
        }
        apu->sample_count = remaining;
    }

    SDL_UnlockMutex(apu->lock);
    return count;
}
