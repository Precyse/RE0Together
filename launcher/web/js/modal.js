export function create(ctx) {
  return { el: document.createElement('div'), update() {}, openConfirm() {}, confirmOpen: () => false, stay() {} };
}
