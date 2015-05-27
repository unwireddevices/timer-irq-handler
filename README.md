# timer-irq-handler
Hardware timer IRQ support on AR9331 SoC

OpenWRT kernel module to provide userspace support for hardware timer interrupts. AR9331 has 4 hardware timers running at sytem bus speed (200 MHz by default). Timer resolution practically achievable by userspace software is 20 us.

# Basic usage:

Initialize timer:

*echo "+ &lt;timer&gt; &lt;tick&gt;" &gt; /sys/kernel/debug/timer-irq*<br />
*echo "+ &lt;timer&gt; &lt;timeout&gt; &lt;pid&gt; [once]" &gt; /sys/kernel/debug/timer-irq*

Remove timer:

*echo "- &lt;timer&gt;" &gt; /sys/kernel/debug/timer-irq*

Parameters:

*timer* — hardware timer number, 0 to 3; timer should not be used by another application or driver

*tick* — timer precision in microseconds; maximum value with 200 MHz system bus is UINT_MAX/(200×10^6)&nbsp;=&nbsp;21&nbsp;474&nbsp;836&nbsp;microseconds; it is not recommended to set tick values above 10&nbsp;secs (10&nbsp;000&nbsp;000&nbsp;microseconds)

*timeout* — timer period; userspace application will receive timer signals every *timeout* microseconds; *timeout* must be bigger than *tick*; maximum timeout is UINT_MAX&nbsp;=&nbsp;4&nbsp;294&nbsp;967&nbsp;295&nbsp;microseconds&nbsp;~&nbsp;71.58&nbsp;minutes

*pid* — userspace application PID to send the signal to (see timer-irq-test.cpp)

*once* — "1" to stop the timer after timeout, omit it to run timer till the end of the Universe or manual removal, whichever comes first

For more info please visit http://www.black-swift.com/wiki
