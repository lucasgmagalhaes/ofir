// Building and splitting a string: the Go baseline of programs/strings.of.
package main

import (
	"fmt"
	"strconv"
	"strings"
)

func main() {
	const n = 1000000
	var b strings.Builder
	for i := int64(0); i < n; i++ {
		b.WriteString(strconv.FormatInt(i*7, 10))
		b.WriteByte(',')
	}
	text := b.String()
	var parts, digits int64
	for _, part := range strings.Split(text, ",") {
		parts++
		digits += int64(len(part))
	}
	fmt.Println(len(text), parts, digits)
}
