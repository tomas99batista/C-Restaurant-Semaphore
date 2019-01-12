#!/bin/bash

rm error*

# change 0x610661c3 to your semaphore and shared memory key
#quando mudares a key para fazeres clean mete a minha em comentário
ipcrm -S  0x613422db
ipcrm -M  0x613422db