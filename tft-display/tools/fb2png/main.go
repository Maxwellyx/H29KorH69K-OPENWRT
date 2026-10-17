// fb2png - convert a raw framebuffer dump (RGB565, 16bpp) to a PNG, so the
// panel content can be looked at from the PC without a camera.
//
//	go run . -in fb.raw -out fb.png [-w 320] [-h 172] [-stride 640] [-swap]
//	go run . -stack -out all.png raw0.raw raw1.raw raw2.raw ...   # 竖着拼成一张
package main

import (
	"bytes"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"os"
)

// 把一张 RGB565 原始帧画进 img 的 (ox, oy) 处；返回错误说明
func drawRGB565(dst *image.RGBA, raw []byte, w, h, stride, ox, oy int, swap bool) error {
	if len(raw) < (h-1)*stride+w*2 {
		return fmt.Errorf("file too small: have %d, need %d", len(raw), (h-1)*stride+w*2)
	}
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			o := y*stride + x*2
			b0, b1 := raw[o], raw[o+1]
			if swap {
				b0, b1 = b1, b0
			}
			v := uint16(b0) | uint16(b1)<<8
			r := uint8((v >> 11) & 0x1F)
			g := uint8((v >> 5) & 0x3F)
			b := uint8(v & 0x1F)
			dst.SetRGBA(ox+x, oy+y, color.RGBA{
				R: r<<3 | r>>2,
				G: g<<2 | g>>4,
				B: b<<3 | b>>2,
				A: 255,
			})
		}
	}
	return nil
}

func writePNG(img image.Image, path string) {
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		fmt.Fprintln(os.Stderr, "png:", err)
		os.Exit(1)
	}
	if err := os.WriteFile(path, buf.Bytes(), 0644); err != nil {
		fmt.Fprintln(os.Stderr, "write:", err)
		os.Exit(1)
	}
	fmt.Printf("wrote %s (%d bytes)\n", path, buf.Len())
}

func main() {
	in := flag.String("in", "fb.raw", "raw framebuffer file (RGB565 LE)")
	out := flag.String("out", "fb.png", "output png")
	w := flag.Int("w", 320, "width")
	h := flag.Int("h", 172, "height")
	stride := flag.Int("stride", 0, "bytes per line (default w*2)")
	swap := flag.Bool("swap", false, "swap byte order per pixel")
	stack := flag.Bool("stack", false, "把后面所有文件名（位置参数）竖着拼成一张图")
	flag.Parse()

	if *stride == 0 {
		*stride = *w * 2
	}

	if *stack {
		files := flag.Args()
		if len(files) == 0 {
			fmt.Fprintln(os.Stderr, "-stack 后面要给 raw 文件")
			os.Exit(1)
		}
		img := image.NewRGBA(image.Rect(0, 0, *w, *h*len(files)))
		for i, f := range files {
			raw, err := os.ReadFile(f)
			if err != nil {
				fmt.Fprintln(os.Stderr, "read:", err)
				os.Exit(1)
			}
			if err := drawRGB565(img, raw, *w, *h, *stride, 0, i**h, *swap); err != nil {
				fmt.Fprintf(os.Stderr, "%s: %v\n", f, err)
				os.Exit(1)
			}
		}
		writePNG(img, *out)
		return
	}

	raw, err := os.ReadFile(*in)
	if err != nil {
		fmt.Fprintln(os.Stderr, "read:", err)
		os.Exit(1)
	}
	img := image.NewRGBA(image.Rect(0, 0, *w, *h))
	if err := drawRGB565(img, raw, *w, *h, *stride, 0, 0, *swap); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	writePNG(img, *out)
}
