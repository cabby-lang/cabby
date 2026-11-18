use std::fmt::Write;
fn main() {
    let n = @N@;
    let mut s = String::new();
    for i in 0..n {
        write!(s, "{}", i).unwrap();
        s.push(' ');
    }
    let bytes = s.as_bytes();
    let mut sevens: i64 = 0;
    for i in 0..bytes.len() {
        if bytes[i] == b'7' { sevens += 1; }
    }
    println!("{} {}", s.len(), sevens);
}
