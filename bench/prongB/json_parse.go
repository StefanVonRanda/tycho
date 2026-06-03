// json-parse prong B — Go (GC). Recursive-descent into a pointer Json tree,
// walk-checksum, discard (garbage collected). K passes over a stdin doc.
package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
)

type Json struct {
	tag  int // 0 null 1 bool 2 num 3 str 4 arr 5 obj
	num  int64
	str  string
	keys []string
	vals []*Json
}

var s []byte
var n, p int

func skipWs() {
	for p < n {
		c := s[p]
		if c == 32 || c == 9 || c == 10 || c == 13 {
			p++
		} else {
			break
		}
	}
}
func parseString() string {
	p++
	var r []byte
	for p < n && s[p] != '"' {
		var c byte
		if s[p] == '\\' {
			p++
			e := s[p]
			switch e {
			case 'n':
				c = '\n'
			case 't':
				c = '\t'
			case '"':
				c = '"'
			case '\\':
				c = '\\'
			default:
				c = e
			}
		} else {
			c = s[p]
		}
		r = append(r, c)
		p++
	}
	p++
	return string(r)
}
func parseNumber() int64 {
	neg := false
	if s[p] == '-' {
		neg = true
		p++
	}
	var v int64 = 0
	for p < n && s[p] >= '0' && s[p] <= '9' {
		v = v*10 + int64(s[p]-'0')
		p++
	}
	if neg {
		return -v
	}
	return v
}
func parseArray() *Json {
	p++
	j := &Json{tag: 4}
	skipWs()
	for p < n && s[p] != ']' {
		j.vals = append(j.vals, parseValue())
		skipWs()
		if p < n && s[p] == ',' {
			p++
			skipWs()
		}
	}
	p++
	return j
}
func parseObject() *Json {
	p++
	j := &Json{tag: 5}
	skipWs()
	for p < n && s[p] != '}' {
		k := parseString()
		skipWs()
		p++
		j.keys = append(j.keys, k)
		j.vals = append(j.vals, parseValue())
		skipWs()
		if p < n && s[p] == ',' {
			p++
			skipWs()
		}
	}
	p++
	return j
}
func parseValue() *Json {
	skipWs()
	c := s[p]
	switch {
	case c == '{':
		return parseObject()
	case c == '[':
		return parseArray()
	case c == '"':
		return &Json{tag: 3, str: parseString()}
	case c == 't':
		p += 4
		return &Json{tag: 1, num: 1}
	case c == 'f':
		p += 5
		return &Json{tag: 1, num: 0}
	case c == 'n':
		p += 4
		return &Json{tag: 0}
	default:
		return &Json{tag: 2, num: parseNumber()}
	}
}
func walk(j *Json) int64 {
	switch j.tag {
	case 2:
		return j.num + 1
	case 3:
		return int64(len(j.str)) + 1
	case 1:
		return 1
	case 0:
		return 1
	case 4:
		var t int64 = 1
		for _, x := range j.vals {
			t += walk(x)
		}
		return t
	case 5:
		var t int64 = 1
		for _, k := range j.keys {
			t += int64(len(k))
		}
		for _, v := range j.vals {
			t += walk(v)
		}
		return t
	}
	return 0
}
func main() {
	buf, _ := io.ReadAll(bufio.NewReader(os.Stdin))
	s = buf
	n = len(buf)
	var acc int64 = 0
	for k := 0; k < 30; k++ {
		p = 0
		j := parseValue()
		acc += walk(j)
	}
	fmt.Printf("checksum=%d\n", acc)
}
