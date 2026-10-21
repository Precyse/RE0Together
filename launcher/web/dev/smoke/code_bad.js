export default async function run(kit) {
  kit.type('#lobby-code', 'not a code');
  kit.click('.code-join');
}
