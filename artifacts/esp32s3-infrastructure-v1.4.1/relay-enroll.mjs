#!/usr/bin/env node
// Offline sealing: stdin only; stdout contains public routing and ciphertext.
// Usage: node scripts/relay-enroll.mjs < private-request.json
import {createHash,generateKeyPairSync,diffieHellman,createPublicKey,hkdfSync,createCipheriv,randomBytes} from 'node:crypto';
import {readFileSync} from 'node:fs';
const canonical=v=>Array.isArray(v)?'['+v.map(canonical).join(',')+']':v&&typeof v==='object'?'{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+canonical(v[k])).join(',')+'}':JSON.stringify(v);
const sha=v=>createHash('sha256').update(v).digest('hex');
export function sealEnrollment(input){
 const {identity,request,secret}=input,p=structuredClone(request);
 if(!identity?.key_id||sha(identity.enroll_public_key)!==identity.key_id||identity.relay_id!==p.relay_id)throw Error('Verify the relay identity and participant fingerprint first');
 const pair=generateKeyPairSync('x25519'),ephemeral=pair.publicKey.export({type:'spki',format:'der'}).subarray(-32),publicKey=createPublicKey({key:Buffer.concat([Buffer.from('302a300506032b656e032100','hex'),Buffer.from(identity.enroll_public_key,'hex')]),format:'der',type:'spki'});
 p.operation??='add';p.sealed={alg:'x25519-hkdf-sha256-chacha20poly1305',key_id:identity.key_id,ephemeral_public_key:ephemeral.toString('hex'),nonce:randomBytes(12).toString('hex'),ciphertext:''};
 const aad=Buffer.from(canonical({relay_id:p.relay_id,enrollment_id:p.enrollment_id,subscriber_id:p.subscriber_id,requested_by:p.requested_by,operation:p.operation,origin:p.origin,subscription_digest:sha(canonical(p.subscription)),key_id:p.sealed.key_id}));
 const shared=diffieHellman({privateKey:pair.privateKey,publicKey}),key=Buffer.from(hkdfSync('sha256',shared,createHash('sha256').update(aad).digest(),Buffer.from('relay-enroll/1'),32)),cipher=createCipheriv('chacha20-poly1305',key,Buffer.from(p.sealed.nonce,'hex'),{authTagLength:16});cipher.setAAD(aad);const plaintext=Buffer.from(canonical(secret));
 p.sealed.ciphertext=Buffer.concat([cipher.update(plaintext),cipher.final(),cipher.getAuthTag()]).toString('hex');plaintext.fill(0);key.fill(0);shared.fill(0);return p;
}
if(process.argv[1]&&import.meta.url===new URL('file://'+process.argv[1]).href){try{console.log(JSON.stringify(sealEnrollment(JSON.parse(readFileSync(0,'utf8')))));}catch{console.error('Enrollment sealing failed; check identity, public request and private input. No secrets printed.');process.exitCode=1;}}
