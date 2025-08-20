import statistics

# Define the file path
file_path = 'exit_duration.txt'

# Initialize a list to store the adjusted durations
adjusted_durations = []

# Open and read the file line by line
with open(file_path, 'r') as file:
    for line in file:
        # Check if the line starts with the relevant text
        if line.startswith("Application exit duration:"):
            # Extract the duration (assumes it is followed by 'ms')
            duration = int(line.split(":")[1].strip().split()[0])
            # Subtract 5000 ms (5 seconds) from each duration
            adjusted_duration = duration - 5000
            adjusted_durations.append(adjusted_duration)

# Calculate the average and standard deviation of the adjusted durations
if adjusted_durations:
    avg_duration = statistics.mean(adjusted_durations)
    stddev_duration = statistics.stdev(adjusted_durations)
    print("Average adjusted duration:", avg_duration, "ms")
    print("Standard deviation of adjusted durations:", stddev_duration, "ms")
else:
    print("No durations found in the file.")
