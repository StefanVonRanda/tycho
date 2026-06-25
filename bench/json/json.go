// json head-to-head, Go port. Same document + checksum as the tycho/C ports.
// encoding/json into a generic interface{} tree -- the idiomatic Go representation of
// an arbitrary JSON value ([]interface{} of map[string]interface{}, numbers float64).
// Held live across the traversal, so peak RSS reflects the whole GC-tracked tree.
package main

import (
	"encoding/json"
	"fmt"
	"strconv"
)

func main() {
	n := 50000
	sb := make([]byte, 0, 1<<20)
	sb = append(sb, '[')
	for i := 0; i < n; i++ {
		if i > 0 {
			sb = append(sb, ',')
		}
		rec := "{\"id\":" + strconv.Itoa(i) +
			",\"cat\":" + strconv.Itoa(i%32) +
			",\"amt\":" + strconv.FormatInt((int64(i)*2654435761)%1000, 10) +
			",\"name\":\"u" + strconv.Itoa(i%1000) + "\"}"
		sb = append(sb, rec...)
	}
	sb = append(sb, ']')

	var v interface{}
	if err := json.Unmarshal(sb, &v); err != nil {
		panic(err)
	}
	arr := v.([]interface{})
	acc := int64(0)
	for _, e := range arr {
		o := e.(map[string]interface{})
		acc += int64(o["id"].(float64)) +
			int64(o["cat"].(float64)) +
			int64(o["amt"].(float64)) +
			int64(len(o["name"].(string)))
	}
	fmt.Println(acc)
}
