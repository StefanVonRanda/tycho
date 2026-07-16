// Raytrace head-to-head — Go reference port. Mirrors raytrace.ty operation-for-
// operation: struct-by-value Vec3, same shading math, same checksum fold.
// Go does not fuse float ops into FMA unless math.FMA is called explicitly, so
// the plain +/-/* here stay IEEE-strict like the C build's -ffp-contract=off.
package main

import (
	"fmt"
	"math"
)

type Vec3 struct{ x, y, z float64 }
type Sphere struct {
	center Vec3
	radius float64
	color  Vec3
}

func vadd(a, b Vec3) Vec3      { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z} }
func vsub(a, b Vec3) Vec3      { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z} }
func vscale(a Vec3, s float64) Vec3 { return Vec3{a.x * s, a.y * s, a.z * s} }
func vdot(a, b Vec3) float64   { return a.x*b.x + a.y*b.y + a.z*b.z }
func vlen(a Vec3) float64      { return math.Sqrt(vdot(a, a)) }
func vnorm(a Vec3) Vec3        { l := vlen(a); return vscale(a, 1.0/l) }

func hitSphere(o, d Vec3, s Sphere) float64 {
	oc := vsub(o, s.center)
	b := vdot(oc, d)
	c := vdot(oc, oc) - s.radius*s.radius
	disc := b*b - c
	if disc < 0.0 {
		return 0.0 - 1.0
	}
	t := 0.0 - b - math.Sqrt(disc)
	if t < 0.001 {
		return 0.0 - 1.0
	}
	return t
}

func clamp01(x float64) float64 {
	if x < 0.0 {
		return 0.0
	}
	if x > 1.0 {
		return 1.0
	}
	return x
}

func main() {
	nx, ny, ns := 1600, 1200, 40

	scene := make([]Sphere, 0, 64)
	for k := 0; k < ns; k++ {
		col := k % 5
		row := k / 5
		cx := (float64(col) - 2.0) * 0.9
		cy := 0.0 - 0.2
		cz := 0.0 - 2.5 - float64(row)*0.8
		rad := 0.35
		cr := float64((k*37)%100) / 100.0
		cg := float64((k*53)%100) / 100.0
		cb := float64((k*97)%100) / 100.0
		scene = append(scene, Sphere{Vec3{cx, cy, cz}, rad, Vec3{cr, cg, cb}})
	}
	scene = append(scene, Sphere{Vec3{0.0, 0.0 - 100.5, 0.0 - 3.0}, 100.0, Vec3{0.6, 0.6, 0.6}})

	light := vnorm(Vec3{0.0 - 1.0, 1.0, 0.5})
	origin := Vec3{0.0, 0.0, 0.0}

	var acc int64 = 0

	for j := 0; j < ny; j++ {
		for i := 0; i < nx; i++ {
			u := (float64(i)/float64(nx))*2.0 - 1.0
			v := 1.0 - (float64(j)/float64(ny))*2.0
			dir := vnorm(Vec3{u * 1.5, v, 0.0 - 1.0})

			best := 1000000.0
			hit := 0 - 1
			for m := 0; m < len(scene); m++ {
				t := hitSphere(origin, dir, scene[m])
				if t > 0.0 {
					if t < best {
						best = t
						hit = m
					}
				}
			}

			r, g, b := 0.0, 0.0, 0.0
			if hit < 0 {
				tt := 0.5 * (dir.y + 1.0)
				r = (1.0 - tt) + tt*0.5
				g = (1.0 - tt) + tt*0.7
				b = (1.0 - tt) + tt*1.0
			} else {
				s := scene[hit]
				p := vadd(origin, vscale(dir, best))
				n := vnorm(vsub(p, s.center))
				diff := vdot(n, light)
				if diff < 0.0 {
					diff = 0.0
				}
				sh := 0.15 + 0.85*diff
				r = s.color.x * sh
				g = s.color.y * sh
				b = s.color.z * sh
			}

			ir := int64(clamp01(r) * 255.0)
			ig := int64(clamp01(g) * 255.0)
			ib := int64(clamp01(b) * 255.0)
			acc = acc + ir + ig*7 + ib*13
		}
	}

	fmt.Printf("%d\n", acc)
}
