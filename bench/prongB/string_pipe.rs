fn main() {
    let m: i64 = 4000; let kk: i64 = 256; let p: i64 = 1000003;
    let mut checksum: i64 = 0;
    for j in 0..m {
        let mut s = String::new();
        for i in 0..kk { s.push((b'0' + ((i + j) % 10) as u8) as char); }
        let h: i64 = s.bytes().map(|b| b as i64).sum();
        checksum = (checksum + h) % p;
    }
    println!("{}", checksum);
}
