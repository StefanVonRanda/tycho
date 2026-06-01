package main

import (
	"fmt"
	"strings"
)

func main() {
	var m, kk, p int64 = 4000, 256, 1000003
	var checksum int64 = 0
	for j := int64(0); j < m; j++ {
		var b strings.Builder
		for i := int64(0); i < kk; i++ {
			b.WriteByte(byte('0' + (i+j)%10))
		}
		s := b.String()
		var h int64 = 0
		for i := 0; i < len(s); i++ {
			h += int64(s[i])
		}
		checksum = (checksum + h) % p
	}
	fmt.Println(checksum)
}
