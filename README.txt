Embedded XY motor controller design project made for the MSP430FR4133.

Operation proceeds in several states. Initially, in the configuration state, the user is prompted for five coordinate pairs, inserted by means of a keypad.
After confirmation of these pairs, the device proceeds to its operation state, where it continuously checks its current position against the desired position. The stepper motors then move in both dimensions toward its target. This movement is reflected in real-time on the onboard LCD.
If at any point during operation any limit sensors are blocked, the blocked state is entered. The motors cease operation and prompt the user to remove the blockage on the appropriate sensor. When all sensors are free, movement is resumed from the platform's position when it exited the operation state (as it returns to that state).
When all coordinate pairs have been visited, the platform returns to its initial position and the device returns to the configuration state.

The DipTrace schematic and PCB layout is available in the pcb/ folder. Screenshots and a demo video are available in the media/ folder.
