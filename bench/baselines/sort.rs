// Sorting a million integers: the Rust baseline of programs/sort.of.
fn main() {
    let n: i64 = 1000000;
    let mut xs: Vec<i64> = Vec::with_capacity(n as usize);
    let mut seed: i64 = 42;
    for _ in 0..n {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        xs.push(seed);
    }
    xs.sort();
    let mut sum: i64 = 0;
    let mut i = 0;
    while i < n {
        sum += xs[i as usize] % 1000 * (i % 7);
        i += 1000;
    }
    let half = (n / 2) as usize;
    println!("{} {} {} {}", xs[0], xs[half], xs[n as usize - 1], sum);
}
