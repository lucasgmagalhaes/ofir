// Counting keys in a hash map: the Rust baseline of programs/map_count.of.
use std::collections::HashMap;

fn main() {
    let n: i64 = 2000000;
    let keys: i64 = 200000;
    let mut counts: HashMap<i64, i64> = HashMap::new();
    let mut seed: i64 = 7;
    for _ in 0..n {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        *counts.entry(seed % keys).or_insert(0) += 1;
    }
    let mut sum: i64 = 0;
    for k in 0..keys {
        if let Some(c) = counts.get(&k) {
            sum += c * (k % 13);
        }
    }
    println!("{} {}", counts.len(), sum);
}
