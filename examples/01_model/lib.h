
static void t3d_mat4_transpose(T3DMat4 *matRes, const T3DMat4 *mat) {
  matRes->m[0][0] = mat->m[0][0];
  matRes->m[1][0] = mat->m[0][1];
  matRes->m[2][0] = mat->m[0][2];
  matRes->m[3][0] = mat->m[0][3];

  matRes->m[0][1] = mat->m[1][0];
  matRes->m[1][1] = mat->m[1][1];
  matRes->m[2][1] = mat->m[1][2];
  matRes->m[3][1] = mat->m[1][3];

  matRes->m[0][2] = mat->m[2][0];
  matRes->m[1][2] = mat->m[2][1];
  matRes->m[2][2] = mat->m[2][2];
  matRes->m[3][2] = mat->m[2][3];

  matRes->m[0][3] = mat->m[3][0];
  matRes->m[1][3] = mat->m[3][1];
  matRes->m[2][3] = mat->m[3][2];
  matRes->m[3][3] = mat->m[3][3];
}

static void t3d_mat4_mul_dir(T3DVec3* vecOut, const T3DMat4 *mat, const T3DVec3* vec)
{
  for(uint32_t i=0; i<3; i++) {
    vecOut->v[i] = mat->m[0][i] * vec->v[0] +
                   mat->m[1][i] * vec->v[1] +
                   mat->m[2][i] * vec->v[2];
  }
}