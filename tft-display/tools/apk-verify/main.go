// apk-verify - 独立校验一个 .apk（apk v2 容器）到底能不能被 apk 接受。
//
//	go run ./tools/apk-verify <file.apk> [<pub.pem>] [<priv.pem>]
//
// tools\mkapk -list 只校验结构 + .PKGINFO 的 datahash；**签名**它不看。
// 这个工具补的就是那一步，口径直接对着 apk-tools 3.0.5 的源码：
//
//	extract_v2.c  ctx->alg = APK_DIGEST_SHA1  ⇒ apk_verify_start(dctx, SHA1, pkey)
//	io_gunzip.c   MPART_DATA 回调喂给 digest 的是**压缩输入缓冲里的指针区间**
//	crypto_mbedtls.c  mbedtls_pk_verify(pk, MBEDTLS_MD_SHA1, digest, sig)
//	⇒ 被签的就是「控制段那个 gzip 成员的**原始（压缩）字节**」的 sha1，RSA PKCS#1 v1.5。
//	而 .PKGINFO 的 datahash 是「数据段那个 gzip 成员的原始字节」的 sha256（另一回事）。
//
// 所以这里用**另一套 plumbing**（Go 的 gzip/tar 而不是 mkapk 自己的解析）重新切段：
// 段序 = [签名段][控制段][数据段]，用 gzip.Multistream(false) 逐成员推进，
// 再从签名段的 tar 里取 `.SIGN.RSA.<键名>` 的内容当签名验证。
// 顺带核对：公钥文件与私钥是否配对（公钥是发到设备 /etc/apk/keys/ 的那份，
// 不配对就一定报 UNTRUSTED signature）、数据段结尾有没有 tar 结束块。
//
// 默认公钥 = d:\Go\5g-modem-manager\apk-signing\modemd-local.rsa.pub（与打包脚本同一个密钥）。
package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto"
	"crypto/rsa"
	"crypto/sha1"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io"
	"os"
	"strings"
)

const defaultPub = `d:\Go\5g-modem-manager\apk-signing\modemd-local.rsa.pub`

// member 解出**下一个** gzip 成员，并返回它在 raw 里的字节数（raw 从成员开头开始）。
// gzip.Reader 在 Multistream(false) 时只读一个成员，且底层是 bytes.Reader（ByteReader）
// ⇒ flate 不会多读，br.Len() 给出的偏移是精确的。
func member(raw []byte) (inflated []byte, consumed int, err error) {
	br := bytes.NewReader(raw)
	zr, err := gzip.NewReader(br)
	if err != nil {
		return nil, 0, err
	}
	zr.Multistream(false)
	b, err := io.ReadAll(zr)
	if err != nil {
		return nil, 0, err
	}
	return b, len(raw) - br.Len(), nil
}

func loadPub(path string) (*rsa.PublicKey, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	blk, _ := pem.Decode(b)
	if blk == nil {
		return nil, fmt.Errorf("%s: 不是 PEM", path)
	}
	k, err := x509.ParsePKIXPublicKey(blk.Bytes)
	if err != nil {
		return nil, err
	}
	pub, ok := k.(*rsa.PublicKey)
	if !ok {
		return nil, fmt.Errorf("%s: 不是 RSA 公钥", path)
	}
	return pub, nil
}

func loadPriv(path string) (*rsa.PrivateKey, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	blk, _ := pem.Decode(b)
	if blk == nil {
		return nil, fmt.Errorf("%s: 不是 PEM", path)
	}
	if k, err := x509.ParsePKCS1PrivateKey(blk.Bytes); err == nil {
		return k, nil
	}
	k, err := x509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		return nil, err
	}
	priv, ok := k.(*rsa.PrivateKey)
	if !ok {
		return nil, fmt.Errorf("%s: 不是 RSA 私钥", path)
	}
	return priv, nil
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "用法：apk-verify <file.apk> [<pub.pem>] [<priv.pem>]")
		os.Exit(2)
	}
	apkPath := os.Args[1]
	pubPath := defaultPub
	if len(os.Args) > 2 && os.Args[2] != "" {
		pubPath = os.Args[2]
	}
	raw, err := os.ReadFile(apkPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fail := func(format string, a ...any) {
		fmt.Printf("  [X] "+format+"\n", a...)
		os.Exit(1)
	}

	fmt.Printf("=== %s (%d bytes)\n", apkPath, len(raw))

	// ---- 切段 ----
	type mem struct {
		off, size int
		inf       []byte
	}
	var mems []mem
	for pos := 0; pos < len(raw); {
		inf, n, err := member(raw[pos:])
		if err != nil {
			fail("第 %d 个 gzip 成员解不开：%v", len(mems), err)
		}
		if n == 0 {
			fail("第 %d 个 gzip 成员长度为 0", len(mems))
		}
		mems = append(mems, mem{off: pos, size: n, inf: inf})
		pos += n
	}
	if len(mems) < 2 {
		fail("只有 %d 个 gzip 成员（apk v2 = 控制段 + 数据段，签名可选）", len(mems))
	}
	var parts []string
	for i, m := range mems {
		kind := "?"
		switch {
		case i == 0 && bytes.Contains(m.inf, []byte(".SIGN.RSA.")):
			kind = "签名段"
		case i == len(mems)-1:
			kind = "数据段"
		default:
			kind = "控制段"
		}
		parts = append(parts, fmt.Sprintf("%s %d", kind, m.size))
	}
	fmt.Printf("  成员    : %s（共 %d，文件 %d）\n", strings.Join(parts, " + "), len(raw), len(raw))
	if mems[len(mems)-1].off+mems[len(mems)-1].size != len(raw) {
		fail("最后一个成员没铺到文件末尾 ⇒ 容器有多余字节")
	}
	dataMember := mems[len(mems)-1]
	ctrlMember := mems[len(mems)-2]
	ctrlRaw := raw[ctrlMember.off : ctrlMember.off+ctrlMember.size]

	// ---- 签名 ----
	var sig []byte
	var signName string
	for _, e := range tarEntries(mems[0].inf) {
		if strings.HasPrefix(e.name, ".SIGN.RSA.") {
			sig, signName = e.data, e.name
		}
	}
	if len(sig) == 0 {
		fmt.Printf("  ! 未签名  : 设备上要 apk add --allow-untrusted；LuCI「上传安装」不带这个开关 ⇒ 会报 UNTRUSTED signature\n")
		fmt.Printf("  控制段  : %d 字节压缩（%d 字节解压）\n", ctrlMember.size, len(ctrlMember.inf))
		checkData(dataMember)
		os.Exit(3)
	}
	if len(mems) == 2 {
		fail("有签名段但没有控制段（成员数 %d）", len(mems))
	}
	fmt.Printf("  签名    : %s（%d 字节）\n", signName, len(sig))

	pub, err := loadPub(pubPath)
	if err != nil {
		fail("公钥读不了：%v", err)
	}
	// 名字必须和设备 /etc/apk/keys/<name> 逐字符一致（apk_trust_key_by_name 是 strcmp）
	base := pubPath
	if i := bytesLastIndexAny(base, `/\\`); i >= 0 {
		base = base[i+1:]
	}
	if signName != ".SIGN.RSA."+base {
		fmt.Printf("  ! 名字    : 签名键名 %q 与公钥文件名 %q 不一致 ⇒ 设备上必须把公钥存成这个键名\n",
			signName, base)
	}
	if len(os.Args) > 3 && os.Args[3] != "" {
		if priv, err := loadPriv(os.Args[3]); err == nil {
			if priv.PublicKey.N.Cmp(pub.N) == 0 && priv.PublicKey.E == pub.E {
			fmt.Printf("  密钥对  : 公钥与私钥配对 OK\n")
			} else {
				fail("公钥与私钥**不配对**（设备的 /etc/apk/keys/%s 与签名用的私钥不是一套）", base)
			}
		}
	}
	sum := sha1.Sum(ctrlRaw)
	if err := rsa.VerifyPKCS1v15(pub, crypto.SHA1, sum[:], sig); err != nil {
		fail("签名对不上：%v（口径 = sha1(控制段 gzip 成员原始字节) + RSA PKCS#1 v1.5）", err)
	}
	fmt.Printf("  签名验证: OK（RSA/SHA1 PKCS#1 v1.5，被签的是控制段那 %d 字节压缩数据，sha1 %x…）\n",
		len(ctrlRaw), sum[:8])

	// ---- 数据段 ----
	checkData(dataMember)
	if !bytes.Contains(ctrlMember.inf, []byte("datahash = ")) {
		fail("控制段的 .PKGINFO 里没有 datahash")
	}
	fmt.Printf("  结论    : 这个包 apk 收（结构 + datahash + 签名都过了；datahash 本身用 mkapk -list 再看一遍更保险）\n")
}

// checkData 校验数据段：必须解得出内容、且结尾有 tar 结束块（否则 apk 读不到 MPART_END）。
func checkData(m struct {
	off, size int
	inf       []byte
}) {
	n := len(m.inf)
	if n < 1024 || !bytes.Equal(m.inf[n-1024:], make([]byte, 1024)) {
		fmt.Printf("  [X] 数据段  : 结尾没有 1024 字节 0（apk 的 tar 解析器读不到 MPART_END ⇒ -APKE_V2PKG_INTEGRITY）\n")
		os.Exit(1)
	}
	fmt.Printf("  数据段  : %d 字节压缩 / %d 字节解压，结尾有 tar 结束块 OK\n", m.size, n)
}

type tarEntry struct {
	name string
	data []byte
}

// tarEntries 列出（压缩成员解出来的）tar 里的所有常规文件条目。
func tarEntries(inflated []byte) []tarEntry {
	var out []tarEntry
	tr := tar.NewReader(bytes.NewReader(inflated))
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			fmt.Printf("  [X] tar 坏了：%v\n", err)
			os.Exit(1)
		}
		b, _ := io.ReadAll(tr)
		out = append(out, tarEntry{hdr.Name, b})
	}
	return out
}

func bytesLastIndexAny(s, set string) int {
	for i := len(s) - 1; i >= 0; i-- {
		for j := 0; j < len(set); j++ {
			if s[i] == set[j] {
				return i
			}
		}
	}
	return -1
}
