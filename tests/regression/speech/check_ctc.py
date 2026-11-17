"""Independent exhaustive-path oracle for the native CTC implementation."""
import argparse
import itertools
import json
import math
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument("executable")
args=parser.parse_args()
run=subprocess.run([args.executable],capture_output=True,text=True,timeout=10,check=True)
observed=json.loads(run.stdout)
logits=[[.2,.5,-.1],[-.3,.7,.4],[.6,-.2,.9],[.1,.3,.8]]


def collapse(path):
    result=[]
    previous=None
    for symbol in path:
        if symbol and symbol!=previous:
            result.append(symbol)
        previous=symbol
    return result


def loss(values):
    probabilities=[]
    for row in values:
        total=sum(math.exp(x) for x in row)
        probabilities.append([math.exp(x)/total for x in row])
    total=sum(math.prod(probabilities[t][c] for t,c in enumerate(path))
              for path in itertools.product(range(3),repeat=4) if collapse(path)==[1,2])
    return -math.log(total)


expected=loss(logits)
assert abs(observed["loss"]-expected)<1e-7,(observed["loss"],expected)
for index,actual in enumerate(observed["gradient"]):
    t,c=divmod(index,3)
    logits[t][c]+=1e-5
    plus=loss(logits)
    logits[t][c]-=2e-5
    minus=loss(logits)
    logits[t][c]+=1e-5
    assert abs(actual-(plus-minus)/2e-5)<2e-7,(index,actual,(plus-minus)/2e-5)
assert observed["alignment"][0]==0 and observed["alignment"][-1]==1
print(json.dumps({"ctc_exhaustive_loss":expected,"gradient_checks":12,"status":"pass"}))
