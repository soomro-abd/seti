#! /bin/bash

task_dur=60	# each test will run for this many seconds
duration=60	# multiplier of task_dur

for t in $(seq 0 $duration)
do
	sudo stress-ng -r 8 --timeout $task_dur --log-brief
done