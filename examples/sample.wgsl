@compute @workgroup_size(8, 1, 1)
fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    var value: u32 = global_id.x;
    if (value < 4u) {
        value += 1u;
    }
}