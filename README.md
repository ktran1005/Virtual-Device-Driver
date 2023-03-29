# Virtual-Device-Driver

# Overview
This project covered **debugfs, kobjects, spinlocks, mutexes, and misc char devices** to write kernel module called **swapper.**

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
   * The **readonly** attribute is read/write, but it can only take on the values of **0** or **1** –
trying to write anything else to this attribute produces **-EINVAL**. If set to **0**, then the
swapstore can be read from and written to; otherwise, if it is set to **1** the swapstore can
only be read from. Attempting to write to **/dev/swapper** when the active swapstore has
readonly set to 1 will produce **-EPERM**. <br />
   * The **removable** attribute can only be read. All swapstores that are inserted using
debugfs will have this attribute set to **1** upon creation. The only swapstore that will
have this set to **0** is the **"default"** swapstore. This is a permanent swapstore that is
built into the hardware, so it can't be ejected. It is created when the swapper module
is loaded and only gets destroyed when the swapper module is unloaded.

# Implementation
## Step 1 - Build the swapstore object
A swapstore looks like this: <br />
```c
struct swapstore {
  struct kobject kobj;
  char data[PAGE_SIZE];
  int readonly;
  int removable;
};
```
First building **kobj_type** that exposes the members **removable** and **readonly** as attributes to userspace first. All swapstore objects should belong to a **kset** that shows up in userspace at: **/sys/kernel/swapstore/** <br />

Create a few swapstores in your init function and make sure you can see them in sysfs under the **kset**. Test them, make sure you can read and write values to them, as appropriate. <br />
Remember, you should only be able to read from **removable** attribute, and wrritng to it should produce **-EPERM**. Also, make sure that writing anything other than **0** and **1** to the **readonly** attribute produces **-EINVAL** before continuing. Only the **root** user should be able to access these attributes. <br />

All swapstore objects should free themselves when the module unloads. It shouldn't matter how many you have or what their names are. This is desirable behavior because you will be creating swapstore objects dynamically at runtime. <br />

## Step 2 - Build the debugfs interface
Your debugfs interface should be located at: <br />
**/sys/kernel/debug/swapper** <br />

In this directory, you will add three (3) files: <br />
    * **insert** - (writeable by root only) <br />
        Writting a **name** to this file will result in a swapstore object being created with the specified name. The new swapstore object should have **removable** set to **1** and **readonly** set to **0** upong creation. If a swapstore by that name already exists, **-EINVAL** should be produced. <br />
    * **swapstore** - (read/write by root only) <br />
        Writing the **name** of a swapstore to this file will result in the corresponding swapstore being "attached" to the misc char device **/dev/swapper.** The previously attached swapstore will be attached. Subsequent reads and writes to **/dev/swapper** will be stored in this swapstore. However, if the char device **/dev/swapper** is currently held open by at least once userspace process, no detachment/attachment should occur and instead **-EBUSY** will be produced. <br />
        
        Reading from this file simply produces the name of the currently active (i.e "attached") swapstore. <br />
        
     * **eject** - (writeable by root only) <br />
         Writing a **name** to this file will result in the corresponding swapstore being queued for ejection. If the swapstore is not currently attached to **/dev/swapper**, it will be removed from the system immediately, generating a corresponding removal uevent. <br />
         However, if the swapstore is currently attached to **/dev/swapper** it will instead be removed immediately after becoming detached from **/dev/swapper**, again generating the appropriate removal uevent. If **name** does not correspond to an existing swapstore, **-EINVAL** should be produced.
