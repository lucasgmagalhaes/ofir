// Counting keys in a hash map: the Go baseline of programs/map_count.of.
package main

import "fmt"

func main() {
	const n = 2000000
	const keys = 200000
	counts := make(map[int64]int64)
	var seed int64 = 7
	for i := 0; i < n; i++ {
		seed = (seed*1103515245 + 12345) % 2147483648
		counts[seed%keys]++
	}
	var sum int64
	for k := int64(0); k < keys; k++ {
		if c, ok := counts[k]; ok {
			sum += c * (k % 13)
		}
	}
	fmt.Println(len(counts), sum)
}
