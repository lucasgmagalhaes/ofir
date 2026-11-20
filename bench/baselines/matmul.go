// Dense matrix multiply: the Go baseline of programs/matmul.of.
package main

import "fmt"

func fill(n, seed int) []float64 {
	m := make([]float64, n*n)
	for i := range m {
		m[i] = float64((i*seed)%17) - 8.0
	}
	return m
}

func multiply(a, b, c []float64, n int) {
	for i := 0; i < n; i++ {
		for k := 0; k < n; k++ {
			aik := a[i*n+k]
			for j := 0; j < n; j++ {
				c[i*n+j] += aik * b[k*n+j]
			}
		}
	}
}

func main() {
	n := 500
	a := fill(n, 7)
	b := fill(n, 5)
	c := make([]float64, n*n)
	multiply(a, b, c, n)
	trace, total := 0.0, 0.0
	for i := 0; i < n; i++ {
		trace += c[i*n+i]
	}
	for _, x := range c {
		total += x
	}
	fmt.Printf("%.1f %.1f\n", trace, total)
}
