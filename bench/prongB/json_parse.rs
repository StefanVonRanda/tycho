// json-parse prong B — Rust. Recursive-descent parse into an owned `Json` enum,
// walk-checksum, drop (RAII). K parse-and-discard passes over a stdin doc.
use std::io::Read;

enum Json { Null, Bool(bool), Num(i64), Str(String), Arr(Vec<Json>), Obj(Vec<String>, Vec<Json>) }

struct P<'a> { s: &'a [u8], pos: usize, n: usize }
impl<'a> P<'a> {
    fn skip_ws(&mut self) { while self.pos < self.n { let c = self.s[self.pos]; if c==32||c==9||c==10||c==13 { self.pos += 1; } else { break; } } }
    fn parse_string(&mut self) -> String {
        self.pos += 1; let mut r = String::new();
        while self.pos < self.n && self.s[self.pos] != b'"' {
            let c = if self.s[self.pos] == b'\\' { self.pos += 1; let e = self.s[self.pos];
                match e { b'n'=>b'\n', b't'=>b'\t', b'"'=>b'"', b'\\'=>b'\\', _=>e } } else { self.s[self.pos] };
            r.push(c as char); self.pos += 1;
        }
        self.pos += 1; r
    }
    fn parse_number(&mut self) -> i64 {
        let mut neg = false; if self.s[self.pos] == b'-' { neg = true; self.pos += 1; }
        let mut v: i64 = 0;
        while self.pos < self.n && self.s[self.pos] >= b'0' && self.s[self.pos] <= b'9' { v = v*10 + (self.s[self.pos]-b'0') as i64; self.pos += 1; }
        if neg { -v } else { v }
    }
    fn parse_array(&mut self) -> Json {
        self.pos += 1; let mut xs = Vec::new(); self.skip_ws();
        while self.pos < self.n && self.s[self.pos] != b']' {
            xs.push(self.parse_value()); self.skip_ws();
            if self.pos < self.n && self.s[self.pos] == b',' { self.pos += 1; self.skip_ws(); }
        }
        self.pos += 1; Json::Arr(xs)
    }
    fn parse_object(&mut self) -> Json {
        self.pos += 1; let mut ks = Vec::new(); let mut vs = Vec::new(); self.skip_ws();
        while self.pos < self.n && self.s[self.pos] != b'}' {
            let k = self.parse_string(); self.skip_ws(); self.pos += 1;
            ks.push(k); vs.push(self.parse_value()); self.skip_ws();
            if self.pos < self.n && self.s[self.pos] == b',' { self.pos += 1; self.skip_ws(); }
        }
        self.pos += 1; Json::Obj(ks, vs)
    }
    fn parse_value(&mut self) -> Json {
        self.skip_ws();
        match self.s[self.pos] {
            b'{' => self.parse_object(),
            b'[' => self.parse_array(),
            b'"' => Json::Str(self.parse_string()),
            b't' => { self.pos += 4; Json::Bool(true) },
            b'f' => { self.pos += 5; Json::Bool(false) },
            b'n' => { self.pos += 4; Json::Null },
            _ => Json::Num(self.parse_number()),
        }
    }
}
fn walk(j: &Json) -> i64 {
    match j {
        Json::Num(x) => x + 1,
        Json::Str(s) => s.len() as i64 + 1,
        Json::Bool(_) => 1,
        Json::Null => 1,
        Json::Arr(xs) => { let mut t = 1i64; for x in xs { t += walk(x); } t },
        Json::Obj(ks, vs) => { let mut t = 1i64; for k in ks { t += k.len() as i64; } for v in vs { t += walk(v); } t },
    }
}
fn main() {
    let mut buf = Vec::new(); std::io::stdin().read_to_end(&mut buf).unwrap();
    let mut acc: i64 = 0;
    for _ in 0..30 {
        let mut p = P { s: &buf, pos: 0, n: buf.len() };
        let j = p.parse_value();
        acc += walk(&j);
    }
    println!("checksum={}", acc);
}
