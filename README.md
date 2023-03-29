# Virtual-Device-Driver

# Overview
This project coveres **debugfs, kobjects, spinlocks, mutexes, and misc char devices** to write kernel module called **swapper.**

The swapper module attempts to emulate the behavior of a simple, yet somewhat strange,
piece of hardware that supports reading and writing to the multiple drives it manages. These
drives are called swapstores. Since this drive isn't real, it doesn't have any buttons we can
press or slots we can insert swapstores into. So, we are going to use debugfs to emulate all
that stuff. You will be able to insert new swapstores, eject swapstores, and set the active
swapstore by writing to different files in debugfs.

The swapper module exposes a single misc device at **/dev/swapper**. Writing to **/dev/swapper**
writes to the currently active swapstore, and reading from /dev/swapper reads from the
currently active swapstore. Again, the currently active swapstore is set through debugfs and
swapstores can be inserted and ejected through debugfs. Only one swapstore can be active at
a time, so think of the swapper device as being like one of those multi-CD changers you would
find in a car.

As you may have guessed, a swapstore is built around a kobject. Each swapstore can store up
to **PAGE_SIZE** bytes (probably 4096 on your system) and has two (2) attributes of interest that
are exposed to userspace: **readonly** and **removable**. <br />
    *The **readonly** attribute is read/write, but it can only take on the values of **0** or **1** –
trying to write anything else to this attribute produces **-EINVAL**. If set to **0**, then the
swapstore can be read from and written to; otherwise, if it is set to **1** the swapstore can
only be read from. Attempting to write to **/dev/swapper** when the active swapstore has
readonly set to 1 will produce **-EPERM**. <br />
    *The **removable** attribute can only be read. All swapstores that are inserted using
debugfs will have this attribute set to **1** upon creation. The only swapstore that will
have this set to **0** is the **"default"** swapstore. This is a permanent swapstore that is
built into the hardware, so it can't be ejected. It is created when the swapper module
is loaded and only gets destroyed when the swapper module is unloaded.
