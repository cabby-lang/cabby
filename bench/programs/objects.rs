trait Shape { fn area(&self) -> i64; }
struct Square { size: i32 }
struct Triangle { size: i32 }
impl Shape for Square { fn area(&self) -> i64 { self.size as i64 * self.size as i64 } }
impl Shape for Triangle { fn area(&self) -> i64 { self.size as i64 * self.size as i64 / 2 } }

#[inline(never)]
fn measure(s: &dyn Shape) -> i64 { s.area() }

fn main() {
    let n = @N@;
    let mut total: i64 = 0;
    for i in 0..n {
        // Boxed: on the heap, like Cabby's objects.
        let s: Box<dyn Shape> = if i % 2 == 0 {
            Box::new(Square { size: i % 100 })
        } else {
            Box::new(Triangle { size: i % 100 })
        };
        total += measure(s.as_ref());
    }
    println!("{}", total);
}
