import numpy as np
from numpy import genfromtxt
import matplotlib.pyplot as plt
import math
import colorsys
import sys

# main
def main():
	data_path = sys.argv[1]
	fig, ax = plt.subplots()
	data = genfromtxt(data_path, delimiter=',')
	for i_stride in range(9): #[0:32:256]MB
		x = data[i_stride:32*9:9, 0]
		y = data[i_stride:32*9:9, 2]
		label_name = "Strided offset: " + str(i_stride * 32) + "MB"
		ax.plot(x,y,label=label_name)
	ax.set_xlabel('#Kernel', fontweight='bold')
	ax.set_ylabel('HBM Throughput (GB/s)', fontweight='bold')
	ax.set_xlim([1, 32])
	ax.legend()

	fig_path = data_path[:-4] + ".pdf"
	fig.show()
	fig.savefig(fig_path)

if __name__ == '__main__':
	main()
