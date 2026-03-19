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


def send_cmd(queue, cmd_str: str):
    """Send a raw state-machine command string to the Teensy."""
    queue.put((cmd_str + "\n").encode("UTF-8"))


def configure_auto_blank(queue,
                         radius_mm: float = 4.5,
                         threshold_mm_per_s: float = 0.1,
                         index_visible: int = 0,
                         index_blank: int = 2048,
                         enabled: int = 1):
    """Teensy cmd 16: configure auto-blanking parameters and enable state."""
    send_cmd(
        queue,
        f"5,16,{radius_mm},{threshold_mm_per_s},{index_visible},{index_blank},{enabled}"
    )


def set_index(queue, index_value: int):
    """Teensy cmd 7: set index DAC."""
    send_cmd(queue, f"1,7,{int(index_value)}")


def run(queue,
        duration_s: float = 60 * 3,
        radius_mm: float = 4.5,
        threshold_mm_per_s: float = 0.1,
        index_visible: int = 0,
        index_blank: int = 2048):
    """
    Auto-blanking trial:
      - start with visible bar
      - configure and enable auto-blanking
      - run for duration_s
      - disable auto-blanking
      - restore visible bar
    """

    set_index(queue, index_visible)

    configure_auto_blank(
        queue,
        radius_mm=radius_mm,
        threshold_mm_per_s=threshold_mm_per_s,
        index_visible=index_visible,
        index_blank=index_blank,
        enabled=1,
    )

    sleep(duration_s)

    configure_auto_blank(
        queue,
        radius_mm=radius_mm,
        threshold_mm_per_s=threshold_mm_per_s,
        index_visible=index_visible,
        index_blank=index_blank,
        enabled=0,
    )

    set_index(queue, index_visible)

    return