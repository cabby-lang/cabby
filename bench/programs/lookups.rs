use std::collections::HashMap;

fn main() {
    let n: i32 = @N@;
    let half = n / 2;
    let mut counts: HashMap<i32, i32> = HashMap::new();
    for i in 0..n {
        let k = i * 7 % half;
        if counts.contains_key(&k) {
            *counts.get_mut(&k).unwrap() += 1;
        } else {
            counts.insert(k, 1);
        }
    }
    let mut sum: i64 = 0;
    for (k, v) in &counts {
        sum += (*k as i64) * (*v as i64);
    }
    for i in 0..half / 2 {
        counts.remove(&(i * 2));
    }
    let mut words: HashMap<String, i32> = HashMap::new();
    for i in 0..n / 10 {
        let w = format!("w{}", i % 1000);
        if words.contains_key(&w) {
            *words.get_mut(&w).unwrap() += 1;
        } else {
            words.insert(w, 1);
        }
    }
    println!("{} {} {} {}", counts.len(), sum, words.len(), words["w7"]);
}
