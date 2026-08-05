"""
File purpose:
Protocol for running a motion-threshold-based auto-blanking trial with hysteresis.

Hysteresis adds a dead zone between a high (turn-on) and low (turn-off) threshold:
- metric > threshold_high → bar VISIBLE  (fly clearly walking)
- metric < threshold_low  → bar BLANK    (fly clearly stopped)
- threshold_low ≤ metric ≤ threshold_high → hold current state (fly grooming / micromotion)

This prevents fly leg adjustments and grooming from flickering the bar on and off
during periods when the fly is nominally stopped.

Relationships to other files:
- AL_auto_blank.py: original single-threshold version (kept for reference / fallback)
- FTHandler.h / FTHandler.cpp: hysteresis logic in update_index_from_motion_threshold()
- teensy_control_v3.ino case 16: now expects 6 params (added threshold_low)

Tuning guide
------------
threshold_high_mm_per_s:
    Speed the fly must clearly exceed to be counted as walking (bar turns ON).
    Must be above fly micromotion / grooming noise (~3-5 mm/s). Start at 7.0 mm/s.
    - Too low  → brief micromotion flickers bar on during stopped periods
    - Too high → slow walking never triggers bar (bar stays blank while walking)

threshold_low_mm_per_s:
    Speed below which the fly is counted as stopped (bar BLANKS).
    Must be above FicTrac noise floor (~2 mm/s). Start at 3.0 mm/s.
    - Too low  → FicTrac noise keeps metric above threshold → bar never blanks
    - Too high → bar blanks even during very slow walking (shrinks dead zone)

Dead zone = threshold_high − threshold_low. Wider dead zone = more stable but
less responsive. Narrower dead zone approaches single-threshold behaviour.

MOTION_WINDOW_SAMPLES (in FTHandler.h — requires reflash):
    65 samples (~0.5 s lag at 108 fps). Each spike contributes 0.5% to the mean.
    Increase if transitions still flicker; decrease if blanking feels too sluggish.
"""

from time import sleep

# ----------------------------
# Teensy command IDs
# ----------------------------
SET_INDEX_CMD_ID   = 7
AUTO_BLANK_CMD_ID  = 16

# ----------------------------
# Command packet sizes
# ----------------------------
SET_INDEX_NUM_PARAMS   = 1
# old: AUTO_BLANK_NUM_PARAMS = 5  (single-threshold, no hysteresis)
AUTO_BLANK_NUM_PARAMS  = 6   # hysteresis version: added threshold_low param

# ----------------------------
# Default index values
# ----------------------------
DEFAULT_INDEX_VISIBLE = 0
DEFAULT_INDEX_BLANK   = 4095


def send_cmd(queue, cmd_str: str):
    """Send a raw state-machine command string to the Teensy."""
    queue.put((cmd_str + "\n").encode("UTF-8"))


def configure_auto_blank(queue,
                         radius_mm:             float = 4.5,
                         threshold_high_mm_per_s: float = 7.0,
                         threshold_low_mm_per_s:  float = 4.0,
                         index_visible: int = DEFAULT_INDEX_VISIBLE,
                         index_blank:   int = DEFAULT_INDEX_BLANK,
                         enabled:       int = 1):
    """
    Teensy cmd 16 (hysteresis version): configure motion-threshold auto-blanking.

    Packet format:
      <num_params>,<cmd_id>,<radius_mm>,<threshold_high>,<threshold_low>,
      <index_visible>,<index_blank>,<enabled>

    Parameters sent to Teensy:
    - radius_mm:              ball radius for converting FicTrac rotation to mm/s
    - threshold_high_mm_per_s: metric must exceed this to show bar (fly walking)
    - threshold_low_mm_per_s:  metric must drop below this to blank bar (fly stopped)
    - index_visible:           index DAC value that shows the bar
    - index_blank:             index DAC value that blanks the bar
    - enabled:                 1 = auto-blanking active, 0 = inactive
    """
    send_cmd(
        queue,
        f"{AUTO_BLANK_NUM_PARAMS},{AUTO_BLANK_CMD_ID},"
        f"{radius_mm},{threshold_high_mm_per_s},{threshold_low_mm_per_s},"
        f"{index_visible},{index_blank},{enabled}"
    )


def set_index(queue, index_value: int):
    """Teensy cmd 7: manually set the index DAC."""
    send_cmd(queue, f"{SET_INDEX_NUM_PARAMS},{SET_INDEX_CMD_ID},{int(index_value)}")


def run(queue,
        duration_s:              float = 60 * 11, # default 1 min for testing; adjust as needed
        radius_mm:               float = 4.5,
        threshold_high_mm_per_s: float = 7.0,  # bar ON above this  (above fly grooming ~3-5 mm/s)
        threshold_low_mm_per_s:  float = 4.0,  # bar BLANK below this (above FicTrac noise ~2 mm/s)
        index_visible: int = DEFAULT_INDEX_VISIBLE,
        index_blank:   int = DEFAULT_INDEX_BLANK):
    """
    Auto-blanking trial with hysteresis:
    - set bar to visible state before starting
    - send hysteresis parameters and enable auto-blanking
    - Teensy holds bar ON while fly walks, blanks when fly stops, ignores micromotion
    - disable auto-blanking at end and restore visible bar
    """

    # Start from a known visible state
    set_index(queue, index_visible)

    configure_auto_blank(
        queue,
        radius_mm=radius_mm,
        threshold_high_mm_per_s=threshold_high_mm_per_s,
        threshold_low_mm_per_s=threshold_low_mm_per_s,
        index_visible=index_visible,
        index_blank=index_blank,
        enabled=1,
    )

    # Let the trial run while FTHandler applies hysteresis threshold in real time
    sleep(duration_s)

    # Turn off auto-blanking
    configure_auto_blank(
        queue,
        radius_mm=radius_mm,
        threshold_high_mm_per_s=threshold_high_mm_per_s,
        threshold_low_mm_per_s=threshold_low_mm_per_s,
        index_visible=index_visible,
        index_blank=index_blank,
        enabled=0,
    )

    # Restore bar to a known visible state
    set_index(queue, index_visible)

    return
