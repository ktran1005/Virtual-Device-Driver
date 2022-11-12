# Virtual-Device-Driver

# Overview
This project coveres debugfs, kobjects, spinlocks, mutexes, and misc char devices to write kernel module called swapper

The swapper module attempts to emulate the behavior of a simple, yet somewhat strange,
piece of hardware that supports reading and writing to the multiple drives it manages. These
drives are called swapstores. Since this drive isn't real, it doesn't have any buttons we can
press or slots we can insert swapstores into. So, we are going to use debugfs to emulate all
that stuff. You will be able to insert new swapstores, eject swapstores, and set the active
swapstore by writing to different files in debugfs.
