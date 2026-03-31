"""
File purpose:
Protocol for running a motion-threshold-based auto-blanking trial.

Main responsibilities:
- send Teensy commands from Python to configure auto-blanking parameters
- enable auto-blanking for the duration of the trial
- let the Teensy decide whether the bar is visible or blank based on FicTrac motion
- disable auto-blanking at the end and restore a visible bar state

Key additions / special logic:
- uses cmd 16 to send the full auto-blanking configuration:
    radius_mm, threshold_mm_per_s, index_visible, index_blank, enabled
- uses cmd 7 to manually set the index DAC before and after the trial
- Python does not compute motion or blanking decisions itself;
  it only sends configuration and on/off commands
- the Teensy/FTHandler backend computes the rolling motion metric and applies
  the threshold logic in real time

Important runtime logic:
- the auto-blanking threshold is runtime-configurable
- changing threshold_mm_per_s here is enough to change the experiment behavior,
  as long as this protocol sends cmd 16 to the Teensy
- no FTHandler.cpp edit is needed just to test a new threshold
- cmd 16 both configures the parameters and sets whether auto-blanking is enabled

Relationships to other files:
- StateSerial parses the command strings sent here
- teensy_control_v3.ino handles cmd 16 and passes parameters into FTHandler
- FTHandler.cpp computes motion from FicTrac data and updates index DAC state
  based on the configured threshold

In short:
This protocol sends runtime auto-blanking settings to the Teensy, turns
motion-based blanking on for the trial, then turns it off and restores the bar.
"""

from time import sleep
# ----------------------------
# Teensy command IDs
# ----------------------------
SET_INDEX_CMD_ID = 7
AUTO_BLANK_CMD_ID = 16

# ----------------------------
# Command packet sizes
# send_cmd format:
#   <num_params>,<cmd_id>,<param1>,...,<paramN>
# where num_params counts only the parameters after cmd_id
# ----------------------------
SET_INDEX_NUM_PARAMS = 1
AUTO_BLANK_NUM_PARAMS = 5

# ----------------------------
# Default index values
# NOTE:
# - index DAC is 0-4095
# - if passed through a gain-of-2 op amp, 4095 corresponds to ~10 V at the panel input
# - choose index_blank based on empirical panel behavior
# ----------------------------
DEFAULT_INDEX_VISIBLE = 0
DEFAULT_INDEX_BLANK = 4095

def send_cmd(queue, cmd_str: str):
    """Send a raw state-machine command string to the Teensy."""
    queue.put((cmd_str + "\n").encode("UTF-8"))


def configure_auto_blank(queue,
                         radius_mm: float = 4.5,
                         threshold_mm_per_s: float = 0.1,
                         index_visible: int = 0,
                         index_blank: int = 4095,
                         enabled: int = 1):

    """
    Teensy cmd 16: configure motion-threshold auto-blanking.

    Packet format:
      <num_params>,<cmd_id>,<radius_mm>,<threshold_mm_per_s>,
      <index_visible>,<index_blank>,<enabled>

    Parameters sent to Teensy:
    - radius_mm: treadmill/ball radius used to convert FicTrac rotation to mm/s
    - threshold_mm_per_s: movement threshold for deciding visible vs blank
    - index_visible: index DAC value that shows the bar
    - index_blank: index DAC value that blanks the bar
    - enabled: 1 = auto-blanking active, 0 = auto-blanking inactive
    """
    send_cmd(
        queue,
        f"{AUTO_BLANK_NUM_PARAMS},{AUTO_BLANK_CMD_ID},"
        f"{radius_mm},{threshold_mm_per_s},{index_visible},{index_blank},{enabled}"
    )


def set_index(queue, index_value: int):
    """Teensy cmd 7: manually set the index DAC."""
    send_cmd(queue, f"{SET_INDEX_NUM_PARAMS},{SET_INDEX_CMD_ID},{int(index_value)}")


def run(queue,
        duration_s: float = 60 * 3,
        radius_mm: float = 4.5,
        threshold_mm_per_s: float = 20.0,
        index_visible: int = DEFAULT_INDEX_VISIBLE,
        index_blank: int = DEFAULT_INDEX_BLANK):
    """
    Auto-blanking trial:
    - set the bar to the visible index before starting
    - send auto-blanking parameters and enable auto-blanking
    - wait for the trial duration while Teensy handles blanking in real time
    - disable auto-blanking at the end of the trial
    - restore the visible bar index
    """

# Start from a known visible state
    set_index(queue, index_visible)
    print("Enabling auto blank:", radius_mm, threshold_mm_per_s, index_visible, index_blank, 1)
    configure_auto_blank(
        queue,
        radius_mm=radius_mm,
        threshold_mm_per_s=threshold_mm_per_s,
        index_visible=index_visible,
        index_blank=index_blank,
        enabled=1,
    )
    
    # Let the trial run while FTHandler applies the motion threshold in real time
    sleep(duration_s)
    print("Disabling auto blank:", radius_mm, threshold_mm_per_s, index_visible, index_blank, 0)
    # Turn off auto-blanking but keep the same config values explicit
    configure_auto_blank(
        queue,
        radius_mm=radius_mm,
        threshold_mm_per_s=threshold_mm_per_s,
        index_visible=index_visible,
        index_blank=index_blank,
        enabled=0,
    )

    # Restore bar to a known visible state after the trial
    set_index(queue, index_visible)

    return