import symemu
import symemu2.events

## What is this leave invoke ?
##
## Leave is not supposed to panic, it's a way to tell parent function that
## there is an error occur with it, and all jobs and resources must immidiately 
## be freed. The leave will combine with the cleanup stack and trap handler,
## create a powerful exception handling with stack and heap object.
##
## Because of that, we don't know what the code of the leave resulted, only
## the parent function know it. This is a hook to get the leave code, for debugging
## and reversing an app/game or a mechanism.
##
## 824932975 is the SID for User::Leave in EPOC9, for example. Entirely, this will 
## get the actual address in EPOC9 and put a breakpoint there.
## 
## Note that a function hook will not occur if no app load the library contains the function.

@symemu2.events.emulatorEpoc9Invoke(824932975)
def leaveHook():
    # r0, when begging the function, contains the leave code. User is a static class
    leaveCode = symemu.cpu.getRegister(0)
    symemu.emulog('Function leaved with code: {}', leaveCode)