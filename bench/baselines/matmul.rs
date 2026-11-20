// Dense matrix multiply: the Rust baseline of programs/matmul.of.
fn fill(n: usize, seed: usize) -> Vec<f64> {
    (0..n * n).map(|i| ((i * seed) % 17) as f64 - 8.0).collect()
}

fn multiply(a: &[f64], b: &[f64], c: &mut [f64], n: usize) {
    for i in 0..n {
        for k in 0..n {
            let aik = a[i * n + k];
            for j in 0..n {
                c[i * n + j] += aik * b[k * n + j];
            }
        }
    }
}

fn main() {
    let n = 500;
    let a = fill(n, 7);
    let b = fill(n, 5);
    let mut c = vec![0.0; n * n];
    multiply(&a, &b, &mut c, n);
    let trace: f64 = (0..n).map(|i| c[i * n + i]).sum();
    let total: f64 = c.iter().sum();
    println!("{:.1} {:.1}", trace, total);
}
