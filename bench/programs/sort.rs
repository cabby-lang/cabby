fn quicksort(a: &mut [i32], lo: i32, hi: i32) {
    if lo >= hi { return; }
    let pivot = a[((lo + hi) / 2) as usize];
    let (mut i, mut j) = (lo, hi);
    while i <= j {
        while a[i as usize] < pivot { i += 1; }
        while a[j as usize] > pivot { j -= 1; }
        if i <= j {
            a.swap(i as usize, j as usize);
            i += 1;
            j -= 1;
        }
    }
    quicksort(a, lo, j);
    quicksort(a, i, hi);
}

fn main() {
    let n: usize = @N@;
    let mut xs = vec![0i32; n];
    let mut seed: i64 = 42;
    for i in 0..n {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        xs[i] = (seed % 1000000) as i32;
    }
    quicksort(&mut xs, 0, n as i32 - 1);
    let mut check: i64 = 0;
    for &x in &xs { check = (check * 31 + x as i64) % 1000000007; }
    println!("{}", check);
}
