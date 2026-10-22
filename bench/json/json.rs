// JSON tree, Rust: recursive descent into an owned Json tree, then fold it.
#[derive(Clone)]
enum Json { Null, Bool(bool), Num(i64), Str(String), Arr(Vec<Json>), Obj(Vec<(String, Json)>) }
struct P<'a> { b: &'a [u8], p: usize }
impl<'a> P<'a> {
    fn ws(&mut self) { while self.p < self.b.len() && matches!(self.b[self.p], b' ' | b'\t' | b'\n' | b'\r') { self.p += 1; } }
    fn value(&mut self) -> Json {
        self.ws();
        match self.b[self.p] {
            b'[' => { self.p += 1; let mut v = Vec::new(); self.ws(); if self.b[self.p] == b']' { self.p += 1; return Json::Arr(v); }
                loop { v.push(self.value()); self.ws(); if self.b[self.p] == b',' { self.p += 1; } else { self.p += 1; break; } }
                Json::Arr(v) }
            b'{' => { self.p += 1; let mut v = Vec::new(); self.ws(); if self.b[self.p] == b'}' { self.p += 1; return Json::Obj(v); }
                loop { self.ws(); let k = self.string(); self.ws(); self.p += 1; let val = self.value(); v.push((k, val)); self.ws();
                    if self.b[self.p] == b',' { self.p += 1; } else { self.p += 1; break; } }
                Json::Obj(v) }
            b'"' => Json::Str(self.string()),
            b't' => { self.p += 4; Json::Bool(true) }
            b'f' => { self.p += 5; Json::Bool(false) }
            b'n' => { self.p += 4; Json::Null }
            _ => self.number(),
        }
    }
    fn string(&mut self) -> String {
        self.p += 1;
        let mut out = Vec::new();
        while self.b[self.p] != b'"' {
            if self.b[self.p] == b'\\' { self.p += 1; }
            out.push(self.b[self.p]); self.p += 1;
        }
        self.p += 1;
        String::from_utf8(out).unwrap()
    }
    fn number(&mut self) -> Json {
        let mut neg = false;
        if self.b[self.p] == b'-' { neg = true; self.p += 1; }
        let mut v: i64 = 0;
        while self.p < self.b.len() && self.b[self.p].is_ascii_digit() {
            v = v * 10 + (self.b[self.p] - b'0') as i64; self.p += 1;
        }
        Json::Num(if neg { -v } else { v })
    }
}
fn get<'a>(j: &'a Json, key: &str) -> &'a Json {
    match j { Json::Obj(kv) => kv.iter().find(|(k, _)| k == key).map(|(_, v)| v).unwrap_or(&Json::Null), _ => &Json::Null }
}
fn num(j: &Json) -> i64 { if let Json::Num(v) = j { *v } else { 0 } }
fn len(j: &Json) -> i64 { match j { Json::Arr(v) => v.len() as i64, Json::Obj(v) => v.len() as i64, Json::Str(s) => s.len() as i64, _ => 0 } }
fn main() {
    let n: i64 = 50000;
    let mut doc = String::with_capacity((n as usize) * 80);
    doc.push('[');
    for i in 0..n {
        if i > 0 { doc.push(','); }
        doc.push_str(&format!("{{\"id\":{},\"cat\":{},\"amt\":{},\"name\":\"u{}\"}}", i, i % 32, (i * 2654435761) % 1000, i % 1000));
    }
    doc.push(']');
    let mut p = P { b: doc.as_bytes(), p: 0 };
    let j = p.value();
    let mut acc = 0;
    if let Json::Arr(items) = &j {
        for e in items {
            acc += num(get(e, "id")) + num(get(e, "cat")) + num(get(e, "amt")) + len(get(e, "name"));
        }
    }
    println!("{}", acc);
}
