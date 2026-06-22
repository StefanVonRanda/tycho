// site benchmark (Go): render N Markdown pages to HTML, transiently -- the same
// work as site.ty, gated by the same FNV-1a-32 checksum. Each render allocates
// strings that become garbage; Go's GC reclaims them, so peak RSS reflects the
// GC's headroom (it holds garbage between collections) rather than the per-page
// working set tycho's arena keeps.
package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

func esc(s string) string {
	var b strings.Builder
	for i := 0; i < len(s); i++ {
		switch s[i] {
		case '&':
			b.WriteString("&amp;")
		case '<':
			b.WriteString("&lt;")
		case '>':
			b.WriteString("&gt;")
		default:
			b.WriteByte(s[i])
		}
	}
	return b.String()
}

func render(body string) string {
	var out strings.Builder
	inList := false
	for _, t := range strings.Split(body, "\n") {
		switch {
		case len(t) == 0:
			if inList {
				out.WriteString("</ul>\n")
				inList = false
			}
		case strings.HasPrefix(t, "## "):
			if inList {
				out.WriteString("</ul>\n")
				inList = false
			}
			out.WriteString("<h2>")
			out.WriteString(esc(t[3:]))
			out.WriteString("</h2>\n")
		case strings.HasPrefix(t, "# "):
			if inList {
				out.WriteString("</ul>\n")
				inList = false
			}
			out.WriteString("<h1>")
			out.WriteString(esc(t[2:]))
			out.WriteString("</h1>\n")
		case strings.HasPrefix(t, "- "):
			if !inList {
				out.WriteString("<ul>\n")
				inList = true
			}
			out.WriteString("<li>")
			out.WriteString(esc(t[2:]))
			out.WriteString("</li>\n")
		default:
			if inList {
				out.WriteString("</ul>\n")
				inList = false
			}
			out.WriteString("<p>")
			out.WriteString(esc(t))
			out.WriteString("</p>\n")
		}
	}
	if inList {
		out.WriteString("</ul>\n")
	}
	return out.String()
}

func main() {
	if len(os.Args) < 3 {
		fmt.Println("usage: site <pages-dir> <N>")
		return
	}
	dir := os.Args[1]
	n, _ := strconv.Atoi(os.Args[2])
	var fnv uint32 = 2166136261
	total := 0
	for i := 0; i < n; i++ {
		data, err := os.ReadFile(fmt.Sprintf("%s/%d.md", dir, i))
		if err != nil {
			fmt.Fprintf(os.Stderr, "cannot read %s/%d.md\n", dir, i)
			os.Exit(1)
		}
		html := render(string(data))
		for j := 0; j < len(html); j++ {
			fnv ^= uint32(html[j])
			fnv *= 16777619
		}
		total += len(html)
	}
	fmt.Printf("pages=%d bytes=%d fnv=%d\n", n, total, fnv)
}
