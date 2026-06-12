import librosa
import matplotlib.pyplot as plt
import numpy as np

# Load the first WAV file
file1 = "sample.wav"
y1, sr1 = librosa.load(file1)

# Load the second WAV file
file2 = "output.wav"
y2, sr2 = librosa.load(file2)

# Create a time array for x-axis
time1 = np.arange(0, len(y1)) / sr1
time2 = np.arange(0, len(y2)) / sr2

# Create a figure
plt.figure(figsize=(8, 4))

# Plot both WAV files on the same graph
plt.plot(time1, y1, color='b', label='Sample')
plt.plot(time2, y2, color='g', label='Output')
plt.title('Audio Comparison')
plt.xlabel('Time (s)')
plt.ylabel('Amplitude')
plt.legend()

plt.tight_layout()
plt.show()
