fn main() {
    let n = @N@;
    let (mut total, mut hits, mut sevens, mut smaller) = (0usize, 0i64, 0i64, 0i64);
    for i in 0..n {
        let line = String::from("  item ") + &i.to_string() + ": the cat sat on the mat  ";
        let trimmed = line.trim().to_string();
        let colon = trimmed.find(':').unwrap();
        let name = trimmed[..colon].to_string();
        let rest = trimmed[colon + 2..].to_string();
        if rest.contains("cat") { hits += 1; }
        let changed = rest.replace("cat", "dog");
        total += changed.len() + name.len();
        if name.to_uppercase().starts_with("ITEM 7") { sevens += 1; }
        if name.as_str() < "item 5" { smaller += 1; }
    }
    println!("{} {} {} {}", total, hits, sevens, smaller);
}
