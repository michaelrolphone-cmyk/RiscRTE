#!/usr/bin/env python3
"""Await the sole trusted lab controller's exact-SHA result; absence is failure."""
import argparse,json,os,time,urllib.request,urllib.error,re
CONTEXT='ESP32-CAM hardware / heartbeat cleanup'
def result(statuses,sha):
    matches=[s for s in statuses if s.get('context')==CONTEXT]
    if not matches:return None
    # GitHub lists newest status first. Never accept an older success after failure.
    state=matches[0].get('state')
    if state=='success':return True
    if state in ('failure','error'):return False
    return None

def main():
    p=argparse.ArgumentParser();p.add_argument('--source-sha',required=True);p.add_argument('--timeout',type=int,default=600);a=p.parse_args()
    if not re.fullmatch('[a-f0-9]{40}',a.source_sha):raise SystemExit('Invalid exact source SHA')
    repo=os.environ['GITHUB_REPOSITORY'];token=os.environ['GITHUB_TOKEN'];deadline=time.monotonic()+min(max(a.timeout,1),600)
    while True:
        url=f'https://api.github.com/repos/{repo}/commits/{a.source_sha}/statuses?per_page=100'
        request=urllib.request.Request(url,headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28'})
        try:
            with urllib.request.urlopen(request,timeout=15) as response:data=json.load(response)
        except (urllib.error.URLError,ValueError) as error:
            raise SystemExit('Hardware result lookup failed: '+str(error))
        status=result(data,a.source_sha)
        if status is True:print('Exact-source CAM heartbeat and verified cleanup passed.');return
        if status is False:raise SystemExit('Trusted CAM execution/cleanup failed. See external status details.')
        if time.monotonic()>=deadline:raise SystemExit('CAM execution unavailable or no terminal exact-source proof; hardware test FAILED.')
        time.sleep(min(15,max(0,deadline-time.monotonic())))
if __name__=='__main__':main()
