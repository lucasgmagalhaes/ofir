// Sorting a million integers: the Go baseline of programs/sort.of.
package main

import (
	"fmt"
	"slices"
)

func main() {
	const n = 1000000
	xs := make([]int64, 0, n)
	var seed int64 = 42
	for i := 0; i < n; i++ {
		seed = (seed*1103515245 + 12345) % 2147483648
		xs = append(xs, seed)
	}
	slices.Sort(xs)
	var sum int64
	for i := int64(0); i < n; i += 1000 {
		sum += xs[i] % 1000 * (i % 7)
	}
	fmt.Println(xs[0], xs[n/2], xs[n-1], sum)
}
