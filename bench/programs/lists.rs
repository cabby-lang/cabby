fn main() {
    let n: i64 = @N@;
    let mut values: Vec<i32> = Vec::new();
    for i in 0..n {
        values.push((i * 7 % 1000) as i32);
    }
    let mut sum: i64 = 0;
    for v in &values {
        sum += *v as i64;
    }
    let found = values.iter().position(|&v| v == 999).map(|p| p as i64).unwrap_or(-1);
    let words: Vec<String> = "alpha beta gamma delta epsilon".split(' ').map(String::from).collect();
    let mut letters: i64 = 0;
    for _ in 0..n / 100 {
        letters += words.join(",").len() as i64;
    }
    println!("{} {} {} {}", values.len(), sum, found, letters);
}
