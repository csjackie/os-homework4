// while (stillChildrenToLaunch or childrenInSystem) {
// 	determine if we should launch a child
// 		if so, launch a child  and update pcb
// 	
// 	check if any blocked process should be changed to ready
// 		if so, set it as unblocked
//
// 	if there is at least one ready process
// 		Select the process at the front of the ready queue
// 		schedule selected process by sending it a message
// 		receive a message back
// 		update appropriate structures
// 		update statistics like total wait time, etc.
// 	else
// 		increment the clock by the appropriate amount
// 		so next iteration would launch a process
//
// 	Every half a second, output the process table and a list
// 	of blocked processes to the screen and the log file
// }
