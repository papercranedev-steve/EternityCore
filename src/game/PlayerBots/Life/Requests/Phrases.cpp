#include "Life/Requests/Phrases.h"
#include "Database/DatabaseEnv.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <set>

namespace Life
{
namespace Phrases
{
    namespace
    {
        struct Line
        {
            char const* key;
            char const* tag;
            char const* text;
        };

        // Seeded into the table once. Town trip question (Life/Errands/TownTrip): {places}
        // ("食物商人和训练师"), {why} (errand.why.* of the business on the trip, joined:
        // "补点东西，顺便学新技能"), {gold} (what the trainer costs, may be empty), {leader}.
        // ("trip.ask" replaced "errand.ask", whose lines said lessons on shopping-only trips.)
        // Reward: {quest}.
        Line const DEFAULTS[] = {
            { "trip.ask",        "default",     "我想去{places}那里一趟，{why}，能等我一下吗？" },
            { "trip.ask",        "default",     "{leader}，我去趟{places}，{why}，很快回来，行吗？" },
            { "trip.ask",        "frugal",      "我去趟{places}，{why}，钱我都算好了，能等我一下吗？" },
            { "trip.ask",        "spendthrift", "钱又快花光了……我去{places}那边{why}，等我？" },
            { "trip.ask",        "chatty",      "对了对了，我得去{places}那里一趟，{why}，等我一下好不好？" },
            { "trip.ask",        "quiet",       "去{places}，{why}，马上回。" },
            { "trip.ask",        "lazy",        "唉，又得跑一趟{places}，{why}……等我会儿？" },
            { "errand.why.shop", "default",     "补点东西" },
            { "errand.why.train", "default",    "学新技能" },
            { "errand.why.profession", "default", "学专业" },
            { "errand.why.and",  "default",     "，顺便" },
            { "errand.go",       "default",     "好，我去去就回。" },
            { "errand.go",       "diligent",    "马上就好！" },
            { "errand.go",       "quiet",       "嗯。" },
            { "errand.declined", "default",     "好吧，那下次再说。" },
            { "errand.declined", "lazy",        "正好，我也懒得跑。" },
            { "errand.back",     "default",     "我回来了。" },
            { "errand.back",     "chatty",      "回来啦，东西都买齐了！" },
            // After the trip, the leader out of sight (whispered). {gate}: the gate's name.
            { "meet.ask",        "default",     "我这边办完了。你在哪？我去找你，还是在{gate}等你？" },
            { "meet.ask",        "chatty",      "东西都买好啦！你还在忙吗？要我过去找你，还是我去{gate}等你？" },
            { "meet.ask",        "quiet",       "好了。找你，还是{gate}等？" },
            { "meet.come",       "default",     "好，我这就过去。" },
            { "meet.gate",       "default",     "好，我去{gate}等你。" },
            { "meet.here",       "default",     "好，我就在这儿等你。" },
            { "meet.arrived",    "default",     "我到{gate}了，你忙完过来就行。" },
            { "meet.arrived",    "lazy",        "我在{gate}坐着呢，不急。" },
            { "meet.opt.come",   "default",     "来找我" },
            { "meet.opt.gate",   "default",     "去{gate}等我" },
            { "meet.opt.here",   "default",     "原地等我" },
            { "place.gate",      "default",     "城门口" },
            { "place.here",      "default",     "原地" },
            { "reward.ask",      "default",     "{quest}的奖励我拿哪个好？" },
            { "reward.ask",      "frugal",      "{quest}的奖励，挑个最值钱的吧？你帮我看看。" },
            // Class quests (Companions/ClassQuests/ClassQuestFlow). {quest}: the chain's last quest,
            // {leader}; thanks: {affinity} (now, of 100).
            { "classquest.ask",      "default", "{leader}，我到等级了，能带我去做职业任务「{quest}」吗？" },
            { "classquest.ask",      "chatty",  "{leader}{leader}！我听说「{quest}」能学到新本事，你陪我去一趟好不好？" },
            { "classquest.ask",      "quiet",   "「{quest}」，能带我去吗？" },
            { "classquest.ask",      "lazy",    "那个「{quest}」……一个人去太累了，你带带我？" },
            { "classquest.askgear",  "default", "{leader}，有个职业任务「{quest}」能拿件装备，有空陪我去吗？" },
            { "classquest.accepted", "default", "太好了！任务我写给你看了，照着走就行。" },
            { "classquest.accepted", "quiet",   "谢了。" },
            { "classquest.declined", "default", "好吧，那下次进城再说。" },
            { "classquest.giveup",   "default", "算了，我以后自己想办法吧。" },
            { "classquest.giveup",   "lazy",    "那就先不做了，反正也不急。" },
            { "classquest.dropped",  "default", "那这个任务我先放着了。" },
            { "classquest.step",     "default", "这一步完成了，下一步看任务日志。" },
            { "classquest.thanks",   "default", "做完了！多亏有你，谢谢！" },
            { "classquest.thanks",   "chatty",  "成啦成啦！没有你我肯定搞不定，太谢谢了！" },
            { "classquest.thanks",   "quiet",   "成了。谢谢。" },
            { "classquest.selfdone", "default", "对了，「{quest}」我自己去做完了。" },
            { "classquest.selfdone", "lazy",    "那个「{quest}」……拖了好久，总算自己做完了。" },
            { "questitem.tame",      "default", "我在驯服它，别打它！" },
            // Conjured supplies (Life/Errands/GiveProvisions, AskProvisions). {leader}; {what}:
            // supply.what.* ("水和面包"); {names}: the bots that ask; {giver}: the mage asked.
            { "supply.offer",        "default", "{leader}，你的{what}不多了，要我给你做点吗？" },
            { "supply.offer",        "chatty",  "{leader}，我看你{what}快没了，我顺手给你变一些吧？" },
            { "supply.offer",        "quiet",   "{what}，要吗？" },
            { "supply.offer.stone",  "default", "{leader}，你身上没有治疗石，要我给你做一块吗？" },
            { "supply.offer.stone",  "quiet",   "治疗石，要吗？" },
            { "supply.declined",     "default", "好，需要的时候跟我说。" },
            { "supply.make",         "default", "稍等，我现做一些。" },
            { "supply.give",         "default", "拿好。" },
            { "supply.give",         "chatty",  "刚做好的，拿去吧！" },
            { "supply.noroom",       "default", "你的包满了，腾点地方我再给你。" },
            { "supply.failed",       "default", "交易没成，等会儿再说吧。" },
            { "supply.askmage",      "default", "{leader}，我的{what}快没了，能帮我做点吗？" },
            { "supply.askmage",      "lazy",    "{leader}，{what}没了……能给我变点吗？" },
            { "supply.askmage.many", "default", "{leader}，{names}的{what}都快没了，能帮我们做点吗？" },
            { "supply.botask",       "default", "{giver}，能给我点{what}吗？" },
            { "supply.botask",       "quiet",   "{giver}，{what}。" },
            { "supply.botack",       "default", "好，稍等。" },
            { "supply.thanks",       "default", "谢了！" },
            { "supply.thanks",       "quiet",   "谢。" },
            { "supply.what.both",    "default", "水和面包" },
            { "supply.what.food",    "default", "面包" },
            { "supply.what.drink",   "default", "水" },
            { "supply.opt.yes",      "default", "要" },
            { "supply.opt.no",       "default", "不用了" },
            { "supply.opt.make",     "default", "给你们做" },
            { "supply.opt.later",    "default", "现在不行" },
            // Artisan trainers out in the world (Companions/ArtisanTrip). {skill}, {trainer}, {gold}, {leader}.
            { "artisan.ask",         "default", "{leader}，我的{skill}可以学大师级了，可城里没人教，得去找{trainer}。有空带我去一趟吗？学费{gold}我自己出。" },
            { "artisan.ask",         "chatty",  "{leader}{leader}！我的{skill}练到头啦，听说{trainer}能教大师级，陪我去一趟好不好？学费我自己出！" },
            { "artisan.ask",         "quiet",   "大师级{skill}，要去找{trainer}。能带我去吗？" },
            { "artisan.ask",         "frugal",  "{leader}，大师级{skill}要{gold}，钱我攒好了，就差你带我去找{trainer}。" },
            { "artisan.accepted",    "default", "太好了！路我记在你任务日志里了，到了附近我自己过去。" },
            { "artisan.accepted",    "quiet",   "谢了。" },
            { "artisan.declined",    "default", "好吧，那下次进城再说。" },
            { "artisan.giveup",      "default", "算了，以后我自己找机会去{trainer}那儿学吧。" },
            { "artisan.giveup",      "lazy",    "那就先不学了，反正也不急。" },
            // Affinity reached a gate (Companions/Affinity/Gates; key = the gate's key). {leader}.
            // A trade the player opened, cancelled (Companions/Affinity/TradeGate). {leader}: the trader.
            { "affinity.trade.early",      "default", "{leader}，不用给我东西，我自己的够用。" },
            { "affinity.trade.early",      "frugal",  "{leader}，不用不用，我不好意思收你的东西。" },
            { "affinity.trade.early",      "quiet",   "不用了，谢谢。" },
            { "affinity.trade.notallowed", "default", "{leader}，钱、吃的喝的、比我身上好的装备、更大的包我收，别的就算了。" },
            { "affinity.trade.notallowed", "chatty",  "{leader}，这些我用不上啊！给我钱、吃喝、更好的装备或者大点的包就行。" },
            { "affinity.trade.notallowed", "quiet",   "只要钱、吃喝、更好的装备和更大的包。" },
            { "affinity.unlock.trade",    "default", "{leader}，以后有好装备、吃的喝的、大点的包，直接交易给我就行。" },
            { "affinity.unlock.trade",    "chatty",  "{leader}！以后有用得上的装备、吃的喝的、大包，直接塞给我，我不客气啦！" },
            { "affinity.unlock.trade",    "quiet",   "装备、吃喝、背包，可以直接给我。" },
            { "affinity.unlock.tradeany", "default", "{leader}，什么东西都可以交易给我，我帮你拿着。" },
            { "affinity.unlock.tradeany", "quiet",   "东西都可以给我。" },
            { "affinity.unlock.bags",     "default", "{leader}，我的包你帮我管着吧，怎么整理都行。" },
            { "affinity.unlock.bags",     "lazy",    "{leader}，我的包你帮我收拾吧，我懒得弄。" },
            { "affinity.unlock.summon",   "default", "{leader}，以后要拉人过来跟我说一声，我来召唤。" },
            { "affinity.unlock.craft",    "default", "{leader}，有材料就给我，我帮你做。" },
            { "affinity.unlock.portal",   "default", "{leader}，想回城跟我说，我给你开传送门。" },
            { "affinity.unlock.money",    "default", "{leader}，钱不够就从我这儿拿吧，别客气。" },
            { "affinity.unlock.money",    "frugal",  "{leader}，我攒的钱你要用就拿，不过省着点花啊。" },
            { "affinity.unlock.rewards",  "default", "{leader}，以后任务奖励你帮我挑吧，你眼光比我好。" },
            { "affinity.unlock.account",  "default", "{leader}，我想一直跟着你。" },
            // Joining the player's account at affinity 100 (Companions/Account/JoinOffer). {leader}.
            { "account.offer",            "default", "{leader}，有件事想跟你说。" },
            { "account.offer",            "quiet",   "{leader}，说句话。" },
            { "account.offer.page",       "default", "跟你出生入死这么久了，我想正式进你的账号。以后你可以亲自带我出去闯，也可以随时叫我来帮忙。你愿意吗？" },
            { "account.offer.page",       "chatty",  "{leader}，我想了好久！让我进你的账号吧，以后你想亲自玩我也行，叫我来帮忙也行，怎么样？" },
            { "account.offer.page",       "quiet",   "……让我进你的账号吧。你愿意吗？" },
            { "account.offer.yes",        "default", "好，欢迎你。" },
            { "account.offer.later",      "default", "现在还不行。" },
            { "account.join.done",        "default", "太好了！以后我就是你账号里的人了。" },
            { "account.join.done",        "chatty",  "太好啦！以后我就是你账号里的人了，多关照！" },
            { "account.join.done",        "quiet",   "嗯。以后一直在。" },
            { "account.join.full",        "default", "你账号里的位置满了……腾出一个再叫我吧，我等你。" },
            { "account.later",            "default", "没关系，想好了就在伙伴名单里叫我一声。" },
            { "account.later",            "quiet",   "好，等你。" },
            { "talk.opt.account",         "default", "到我的账号里来吧？" },
            // A friend trait rolled at affinity 80 / 90 (Companions/Affinity/Traits). {leader},
            // {trait}: its name. Lines per trait can be added as "affinity.trait.<key>" (said instead).
            { "affinity.trait.life",      "default",  "{leader}，跟着你这些日子，我也摸出了点自己的门道。（{trait}）" },
            { "affinity.trait.life",      "chatty",   "{leader}！我发现跟你待久了，自己都变了好多，你看出来没？（{trait}）" },
            { "affinity.trait.life",      "quiet",    "跟着你，学到了点东西。（{trait}）" },
            { "affinity.trait.life",      "homebody", "{leader}，跟你在外面跑多了，我也没那么想家了。（{trait}）" },
            { "affinity.trait.combat",    "default",  "{leader}，跟你出生入死这么多回，我身手也练出来了。（{trait}）" },
            { "affinity.trait.combat",    "chatty",   "{leader}！这么多场硬仗打下来，我现在可不比从前啦！（{trait}）" },
            { "affinity.trait.combat",    "quiet",    "打了这么多仗，我也练出来了。（{trait}）" },
            { "affinity.trait.combat",    "lazy",     "{leader}，被你拉着打了这么多仗，想偷懒都偷不成，倒是练出来了。（{trait}）" },
            { "affinity.trait.gatherer",  "default",  "{leader}，跟你混了这么久，采草挖矿都顺手多了。" },
            { "affinity.trait.nightowl",  "default",  "{leader}，夜里我精神最好，歇一会儿就缓过来了。" },
            { "affinity.trait.laststand", "default",  "{leader}，放心，我没那么容易倒下。" },
            { "artisan.thanks",      "default", "学会大师级{skill}了！多亏你带我来，谢谢！" },
            { "artisan.thanks",      "chatty",  "大师级{skill}到手啦！没有你我自己肯定找不到这儿，太谢谢了！" },
            { "artisan.thanks",      "quiet",   "学会了。谢谢。" },
            { "artisan.selfdone",    "default", "对了，大师级{skill}我自己去学了。" },
            { "artisan.opt.yes",     "default", "带你去" },
            { "artisan.opt.no",      "default", "现在不行" },
            { "classquest.opt.yes",  "default", "带你去" },
            { "classquest.opt.no",   "default", "现在不行" },
            // Talking to him (Companions/Talk). Greetings by affinity (new < 20 <= friend < 50 <=
            // trusted < 80 <= close) or how he is (drink: low on mana; town: in a capital). {leader}.
            { "talk.hello.new",      "default", "你好，{leader}。有什么事吗？" },
            { "talk.hello.new",      "chatty",  "啊，{leader}！找我有事？尽管说！" },
            { "talk.hello.new",      "quiet",   "……有事？" },
            { "talk.hello.friend",   "default", "{leader}，怎么了？" },
            { "talk.hello.friend",   "chatty",  "来啦来啦，要我帮什么忙？" },
            { "talk.hello.friend",   "lazy",    "嗯……什么事？" },
            { "talk.hello.trusted",  "default", "{leader}，你说。" },
            { "talk.hello.trusted",  "diligent", "{leader}，有什么要我做的？" },
            { "talk.hello.close",    "default", "老朋友，又有什么事？" },
            { "talk.hello.close",    "chatty",  "{leader}！正想找你聊聊呢，什么事？" },
            { "talk.hello.close",    "lazy",    "又是你啊……说吧。" },
            { "talk.hello.drink",    "default", "等我喝口水……好了，什么事？" },
            { "talk.hello.drink",    "quiet",   "（喝了口水）嗯？" },
            { "talk.hello.town",     "default", "总算回来了。什么事？" },
            { "talk.hello.town",     "chatty",  "城里真热闹！找我干嘛？" },
            { "talk.hello.town",     "homebody", "还是城里舒服。{leader}，什么事？" },
            { "talk.busy",           "default", "现在顾不上！" },
            { "talk.busy",           "quiet",   "等会儿！" },
            { "talk.gone",           "default", "嗯？你刚说什么来着？" },
            { "talk.stay.ok",        "default", "好，我就在这儿等你。" },
            { "talk.stay.ok",        "lazy",    "正好歇会儿。" },
            { "talk.stay.ok",        "quiet",   "嗯，等你。" },
            { "talk.follow.ok",      "default", "走吧。" },
            { "talk.follow.ok",      "chatty",  "走走走，等得我都快睡着了！" },
            { "talk.stay.cannot",    "default", "现在不行。" },
            { "talk.stay.team",      "default", "好，我们在这儿等你。" },
            { "talk.follow.team",    "default", "来了！" },
            { "talk.bags.ok",        "default", "看吧。" },
            // A mage's portals (Companions/Talk/Services/PortalService).
            { "talk.portal.where",   "default", "去哪儿？" },
            { "talk.portal.where",   "chatty",  "想去哪座城？我都熟！" },
            { "talk.portal.ok",      "default", "好，稍等，门马上开。" },
            { "talk.portal.ok",      "quiet",   "稍等。" },
            { "talk.portal.cannot",  "default", "这里开不了门。" },
            { "talk.block.portal",   "default", "缺传送门符文" },
            { "talk.bags.ok",        "frugal",  "看可以，别乱动我的东西啊。" },
            // Making things for the player with his materials (Life/Errands/CraftForPlayer). {leader}.
            { "talk.craft.ok",       "default", "你看看要什么，材料你出。" },
            { "talk.craft.ok",       "crafty",  "正好手痒，想要什么尽管挑！" },
            { "craft.start",         "default", "好，这就做。" },
            { "craft.start",         "quiet",   "嗯。" },
            { "craft.done",          "default", "好了，拿去吧。" },
            { "craft.done",          "chatty",  "做好啦！手艺还行吧？" },
            { "craft.stop.nomats",   "default", "材料用完了，就这些。" },
            { "craft.stop.far",      "default", "你走远了，我先停下。" },
            { "craft.stop.combat",   "default", "先打架，回头再做！" },
            { "craft.stop.stopped",  "default", "好，先做到这儿。" },
            { "craft.stop.bagsfull", "default", "我包里放不下了，腾点地方再说。" },
            { "craft.stop.failed",   "default", "这个做不了……可能缺工具，或者得在铁砧、熔炉边上做。" },
            { "craft.stop.cooldown", "default", "这个现在做不了，得过阵子才行。" },
            // He leaves by hearthstone or flight path (Companions/Travel/Departure). {leader}.
            { "leave.ask",           "default", "{leader}，你要走啊？我们在这儿等你？" },
            { "leave.ask",           "chatty",  "{leader}要去哪儿？我们就在这儿等你回来好不好？" },
            { "leave.ask",           "quiet",   "我们在这儿等？" },
            { "leave.follow",        "default", "好，我们跟上。" },
            { "leave.opt.yes",       "default", "在这等我" },
            { "leave.opt.no",        "default", "跟上来" },
            // A waiting warlock summons him back (Companions/Travel/SummonOffer). {leader}, {helpers}.
            { "summon.ask",          "default", "{leader}，办完事了吗？要我把你拉回来吗？" },
            { "summon.ask",          "quiet",   "拉你回来？" },
            { "summon.opt.yes",      "default", "拉我回去" },
            { "summon.opt.no",       "default", "先不用" },
            { "summon.start",        "default", "好，我开门了，{helpers}来帮把手！" },
            { "summon.arrived",      "default", "欢迎回来！" },
            { "summon.arrived",      "chatty",  "回来啦！我们等你好久了！" },
            { "summon.nohelp",       "default", "没人来点门……算了，下次吧。" },
            { "summon.broken",       "default", "仪式被打断了。" },
            { "summon.no.notlearned", "default", "我还不会召唤仪式。" },
            { "summon.no.locked",    "default", "这个……我们还没那么熟吧。" },
            { "summon.no.noshard",   "default", "我没有灵魂碎片了，拉不了你。" },
            { "summon.no.nohelpers", "default", "身边得有两个人帮我点门才行。" },
            { "summon.no.fighting",    "default", "现在打着呢，等会儿！" },
            { "summon.no.here",      "default", "在这里开不了召唤之门。" },
            { "summon.no.near",      "default", "你就在旁边，走过来就行。" },
            { "summon.no.dead",      "default", "现在拉不了。" },
            { "summon.no.busy",      "default", "已经在开门了。" },
            // A companion left behind travels to the team (Life/Errands/Travel). {leader}, {zone}, {minutes}.
            { "travel.start",        "default", "我在{zone}，这就过去，大概{minutes}分钟。" },
            { "travel.start",        "quiet",   "在{zone}，马上来。" },
            { "travel.start",        "chatty",  "{leader}！我在{zone}呢，等我{minutes}分钟，这就跑过去！" },
            { "travel.start.fly",    "default", "我在{zone}，坐鸟过去，大概{minutes}分钟。" },
            { "travel.start.fly",    "quiet",   "在{zone}，坐鸟过去。" },
            { "travel.start.fly",    "lazy",    "我在{zone}……坐鸟过去吧，{minutes}分钟左右。" },
            { "travel.start.sail",   "default", "我在{zone}，得坐船过去，大概{minutes}分钟。" },
            { "travel.start.sail",   "quiet",   "在{zone}，坐船过去。" },
            { "travel.start.hearth", "default", "我在{zone}，先用炉石回{home}，再过去，大概{minutes}分钟。" },
            { "travel.start.hearth", "quiet",   "先回{home}，再过去。" },
            { "travel.start.hearth", "chatty",  "{leader}，我在{zone}！我先炉石回{home}，再赶过去，{minutes}分钟左右！" },
            { "travel.sail",         "default", "上船了。" },
            { "travel.sail",         "chatty",  "上船了！这船晃得厉害……" },
            // Sent away from the party in front of him (Life/Errands/WalkAway, a whisper). {leader}, {home}.
            { "leave.bye",           "default", "那我先走了，有事再叫我。" },
            { "leave.bye",           "quiet",   "走了。有事叫我。" },
            { "leave.bye",           "chatty",  "好吧，那我先走啦！{leader}，想我了就叫我！" },
            { "leave.bye.hearth",    "default", "那我先回{home}了，有事再叫我。" },
            { "leave.bye.hearth",    "quiet",   "回{home}了。" },
            { "leave.bye.hearth",    "homebody", "正好，回{home}歇歇。有事再叫我。" },
            // He bound his hearthstone at an inn (Companions/Travel/HomeBind). {leader}, {home}.
            { "home.bind",           "default", "我也把炉石绑在{home}了。" },
            { "home.bind",           "quiet",   "我也绑这儿。" },
            { "home.bind",           "homebody", "{home}挺好的，我也在这儿安家吧。" },
            // He hearths, ours bound at the same inn (Companions/Travel/HearthAlong). {leader}, {home}.
            { "hearth.along",        "default", "我也回{home}。" },
            { "hearth.along",        "quiet",   "一起回。" },
            { "hearth.along",        "chatty",  "回{home}啦？等等我，一起走！" },
            { "travel.near",         "default", "快到了。" },
            { "travel.near",         "quiet",   "到了。" },
            // The player's words in the talk window (tag "default" only).
            { "talk.opt.craft",      "default", "能帮我做点东西吗？" },
            { "talk.opt.craft.171",  "default", "能帮我配点药水吗？" },
            { "talk.opt.craft.164",  "default", "能帮我打点东西吗？" },
            { "talk.opt.craft.165",  "default", "能帮我做点皮具吗？" },
            { "talk.opt.craft.197",  "default", "能帮我缝点东西吗？" },
            { "talk.opt.craft.202",  "default", "能帮我做点小玩意儿吗？" },
            { "talk.opt.craft.333",  "default", "能帮我做点附魔用的东西吗？" },
            { "talk.opt.craft.129",  "default", "能帮我做点绷带吗？" },
            { "talk.opt.craft.185",  "default", "能帮我做点吃的吗？" },
            { "talk.opt.stay",       "default", "在这儿等我。" },
            { "talk.opt.follow",     "default", "跟我走吧。" },
            { "talk.opt.bags",       "default", "让我看看你的包。" },
            { "talk.opt.portal",     "default", "能帮我开个传送门吗？" },
            { "talk.opt.portal.10059", "default", "去暴风城。" },
            { "talk.opt.portal.11416", "default", "去铁炉堡。" },
            { "talk.opt.portal.11419", "default", "去达纳苏斯。" },
            { "talk.opt.portal.11417", "default", "去奥格瑞玛。" },
            { "talk.opt.portal.11418", "default", "去幽暗城。" },
            { "talk.opt.portal.11420", "default", "去雷霆崖。" },
            // Words and button labels (tag "default" only).
            { "place.vendor",    "default",     "商人" },
            { "place.vendor.repair",  "default", "护甲商" },
            { "place.vendor.ammo",    "default", "弹药商" },
            { "place.vendor.food",    "default", "食物商人" },
            { "place.vendor.reagent", "default", "材料商" },
            { "place.auction",   "default",     "拍卖行" },
            { "place.trainer",   "default",     "训练师" },
            { "place.trainer.profession", "default", "专业训练师" },
            { "place.and",       "default",     "和" },
            { "place.comma",     "default",     "、" },
            { "errand.opt.yes",  "default",     "去吧" },
            { "errand.opt.no",   "default",     "先别去" },
            // Short of money for the trainer (Trip/TrainingPlanner): three answers, the money
            // line under the lessons. {important}, {all}, {have}: money.
            { "errand.opt.pay",  "default",     "给钱去学" },
            { "errand.opt.nopay", "default",    "不给钱去学" },
            { "errand.paid",     "default",     "谢谢！我这就去。" },
            { "errand.paid",     "frugal",      "谢啦，这钱我一定花在刀刃上。" },
            { "training.money",  "default",     "学费：重要技能{important}，全部{all}，我身上有{have}。" },
            // Back after a while away (Companions/OfflineLife). {gains}: "裁缝 +12、草药学 +2",
            // {gold}: what it spent.
            { "offline.skillup",      "default",     "我不在的时候练了练专业：{gains}，材料花了{gold}。" },
            { "offline.skillup",      "crafty",      "这几天手痒，做了不少东西：{gains}，材料花了{gold}。" },
            { "offline.skillup",      "frugal",      "专业练了一点：{gains}，花了{gold}，每个铜板都算过的。" },
            { "offline.skillup",      "spendthrift", "材料买得有点多……{gains}，花了{gold}。" },
            { "offline.skillup",      "quiet",       "{gains}。花了{gold}。" },
            { "offline.skillup.free", "default",     "我不在的时候出去转了转：{gains}。" },
            { "offline.skillup.free", "gatherer",    "这几天到处采东西：{gains}。" },
            { "offline.broke",        "default",     "这阵子手头紧，专业没敢练。" },
            { "offline.broke",        "spendthrift", "钱……都花光了，专业也没练成。" },
            { "offline.broke",        "quiet",       "没钱，专业没练。" },
            // Grew on the bench (Companions/Bench/BenchLeveling). {level}: its level now.
            { "bench.leveled",        "default",     "这阵子我自己也出去练了练，现在{level}级了。" },
            { "bench.leveled",        "chatty",      "你不在的时候我可没闲着！自己出去打了好多怪，现在{level}级啦！" },
            { "bench.leveled",        "quiet",       "自己练到{level}级了。" },
            // Expedition parcel mail (Companions/Bench/Expedition). {leader}, {name}: the companion,
            // {zone}: where it is (".nozone": not known).
            { "parcel.subject",       "default",     "给你带了点东西" },
            { "parcel.subject",       "chatty",      "快打开看看！" },
            { "parcel.subject",       "quiet",       "东西" },
            { "parcel.body",          "default",     "{leader}：\n\n这阵子我在{zone}转了转，顺手攒了点东西，给你寄过去。用得上就拿着。\n\n——{name}" },
            { "parcel.body",          "chatty",      "{leader}！\n\n我在{zone}跑了好几天，打了好多怪，捡了不少好东西，一股脑都寄给你啦！下次见面记得跟我说说你那边的事。\n\n——{name}" },
            { "parcel.body",          "frugal",      "{leader}：\n\n在{zone}攒下的，一个铜板都没乱花。你拿去用吧。\n\n——{name}" },
            { "parcel.body",          "quiet",       "{leader}：\n\n{zone}带回来的。\n\n——{name}" },
            { "parcel.body.nozone",   "default",     "{leader}：\n\n这阵子我出去转了转，顺手攒了点东西，给你寄过去。用得上就拿着。\n\n——{name}" },
            // Profession trainers (Trip/ProfessionPlanner): {skill}, {count}.
            { "training.recipes", "default",    "{skill}：{count}个新配方" },
            // Out in the field (Life/Errands/LessonReminder). {lessons}: the names.
            { "lesson.remind",   "default",     "我现在能学{lessons}了，下次进主城记得带我去趟训练师。" },
            { "lesson.remind",   "chatty",      "对了对了！我能学{lessons}了，进主城的时候陪我去趟训练师好不好？" },
            { "lesson.remind",   "quiet",       "能学{lessons}了。进城时带我去训练师。" },
            { "lesson.remind.again", "default", "{lessons}我还没学呢，进主城的时候别忘了带我去训练师。" },
            { "money.gold",      "default",     "金" },
            { "money.silver",    "default",     "银" },
            { "money.copper",    "default",     "铜" },
            // Class names ({class}).
            { "class.1",         "default",     "战士" },
            { "class.2",         "default",     "圣骑士" },
            { "class.3",         "default",     "猎人" },
            { "class.4",         "default",     "潜行者" },
            { "class.5",         "default",     "牧师" },
            { "class.7",         "default",     "萨满祭司" },
            { "class.8",         "default",     "法师" },
            { "class.9",         "default",     "术士" },
            { "class.11",        "default",     "德鲁伊" },
            // Recruiting (.kiro/docs/2026-11-21-companion-recruiting.md). A stranger talked to
            // (Population/StrangerTalk); {level}: his level.
            { "meet.hello",      "default",     "嗯？找我有事吗？" },
            { "meet.hello",      "chatty",      "哟，你好啊！有什么事？" },
            { "meet.hello",      "quiet",       "……有事？" },
            { "meet.hello",      "lazy",        "嗯……什么事？" },
            { "meet.invited",    "default",     "嗯？怎么突然拉我进队？我们认识吗？" },
            { "meet.invited",    "chatty",      "哎？你拉我干嘛？有什么事先说说嘛。" },
            { "meet.invited",    "quiet",       "……为什么拉我？" },
            { "meet.invite.far", "default",     "我们认识吗？有事过来说吧。" },
            { "meet.opt.join",   "default",     "一起打一会儿怎么样？" },
            { "meet.opt.chat",   "default",     "你在忙什么呢？" },
            { "meet.chat.city",  "default",     "没什么，进城补点东西，顺便歇歇脚。" },
            { "meet.chat.city",  "homebody",    "我就喜欢在城里待着，外面多危险啊。" },
            { "meet.chat.city",  "chatty",      "随便逛逛呗！拍卖行那边今天可热闹了。" },
            { "meet.chat.town",  "default",     "在这附近接了几个活，歇口气再走。" },
            { "meet.chat.town",  "lazy",        "什么都没干，晒晒太阳。" },
            { "meet.chat.field", "default",     "在这儿打点东西，攒点钱。" },
            { "meet.chat.field", "diligent",    "这一片的任务快做完了，再打几只就去下一个地方。" },
            { "meet.join.yes",   "default",     "行啊，正好一个人打得慢。走吧。" },
            { "meet.join.yes",   "chatty",      "好啊好啊！一个人打怪闷死了，走吧！" },
            { "meet.join.yes",   "quiet",       "……好，走吧。" },
            { "meet.join.no",    "default",     "谢了，不过我还有自己的事要忙。" },
            { "meet.join.no",    "lazy",        "今天不想动了，下回吧。" },
            { "meet.join.no",    "homebody",    "我还是在城里待着吧。" },
            { "meet.join.no",    "quiet",       "不了。" },
            { "meet.join.again", "default",     "刚才不是说了吗，今天不行。" },
            { "meet.join.toohigh", "default",   "你还太嫩了点。等你到{level}级再来找我吧。" },
            { "meet.join.toohigh", "chatty",    "哈哈，跟着我你可跟不上！等你到{level}级，我们再一起打。" },
            { "meet.join.toohigh", "quiet",     "到{level}级再说。" },
            { "meet.join.full",  "default",     "你们人已经够多了吧，我就不挤了。" },
            { "meet.join.cannot", "default",    "我就是路过，不跟谁一起走。" },
            // A trial partner (搭伙, Companions/Recruit/Trial).
            { "trial.start",     "default",     "先说好，我就跟你打一阵子，合得来再说。" },
            { "trial.start",     "chatty",      "那就搭个伙！合得来的话说不定我就一直跟着你了。" },
            { "trial.start",     "quiet",       "先一起打一阵子看看。" },
            { "trial.offer",     "default",     "{leader}，跟你一起打挺顺手的。" },
            { "trial.offer",     "chatty",      "{leader}！跟你一起打真痛快！" },
            { "trial.offer.page", "default",    "跟你挺合得来的。要不……以后我就跟着你了？" },
            { "trial.offer.page", "chatty",     "我想了想，一个人打哪有跟你一起有意思。以后带上我呗？" },
            { "trial.offer.page", "quiet",      "……以后，我跟着你，行吗？" },
            { "trial.offer.yes", "default",     "好，以后一起走。" },
            { "trial.offer.later", "default",   "再说吧。" },
            { "talk.opt.bond",   "default",     "以后跟我一起走吧？" },
            { "trial.bond.yes",  "default",     "那就这么说定了！" },
            { "trial.bond.later", "default",    "好，你想好了随时跟我说。" },
            { "trial.intro",     "default",     "重新认识一下：我叫{name}，是个{class}。以后请多关照了，{leader}。" },
            { "trial.intro",     "chatty",      "那我正式介绍一下！我叫{name}，{class}，能吃能打，就是有时候话多了点。以后多关照啦，{leader}！" },
            { "trial.intro",     "quiet",       "{name}，{class}。以后……请多关照。" },
            { "trial.intro",     "frugal",      "我叫{name}，是个{class}。钱我会自己管好，不给你添麻烦。以后多关照，{leader}。" },
            // The player's first companion (Companions/Recruit/FirstCompanion). {giver}: who gave
            // the player his quest; {name}, {class}; {help}: first.help.*.
            { "first.hello",     "default",     "嘿！等一下！你也是刚来报到的吧？" },
            { "first.why",       "default",     "{giver}说这一带最近不太平，新来的最好结伴走。我转了半天，还没人愿意带上我……" },
            { "first.offer",     "default",     "我叫{name}，是个{class}。咱俩搭个伴怎么样？你在前面打，我{help}。" },
            { "first.help.heal", "default",     "帮你治伤" },
            { "first.help.tank", "default",     "帮你顶在前面" },
            { "first.giver",     "default",     "营地的长官" },
            { "first.opt.yes",   "default",     "好啊，一起走吧。" },
            { "first.opt.no",    "default",     "我想先一个人走走。" },
            { "first.yes",       "default",     "太好了！那以后就请多关照了！" },
            { "first.no",        "default",     "好吧……我就在这附近，改主意了再来找我。" },
        };

        struct Template
        {
            std::string tag;
            std::string text;
        };
        std::map<std::string, std::vector<Template>> s_byKey;

        bool EnsureTable()
        {
            CharacterDatabase.DirectExecute(
                "CREATE TABLE IF NOT EXISTS `custom_companion_phrase` ("
                " `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
                " `key` VARCHAR(32) NOT NULL COMMENT '<intent>.<moment>, e.g. errand.ask',"
                " `tag` VARCHAR(24) NOT NULL DEFAULT 'default' COMMENT 'personality tag key or default',"
                " `text` VARCHAR(255) NOT NULL COMMENT '{name} variables',"
                " PRIMARY KEY (`id`), KEY `idx_key` (`key`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: what they say (soul companions doc 5.1)'");
            std::unique_ptr<QueryResult> exists(CharacterDatabase.Query("SHOW TABLES LIKE 'custom_companion_phrase'"));
            return exists != nullptr;
        }

        // Defaults of keys the table has no row of (a key added by newer code; the user's own
        // edits of a key are kept).
        void Seed()
        {
            std::set<std::string> present;
            std::unique_ptr<QueryResult> keys(CharacterDatabase.Query("SELECT DISTINCT `key` FROM `custom_companion_phrase`"));
            if (keys)
            {
                do
                {
                    present.insert(keys->Fetch()[0].GetCppString());
                } while (keys->NextRow());
            }
            for (Line const& line : DEFAULTS)
            {
                if (present.count(line.key))
                    continue;
                std::string key = line.key, tag = line.tag, text = line.text;
                CharacterDatabase.escape_string(key);
                CharacterDatabase.escape_string(tag);
                CharacterDatabase.escape_string(text);
                CharacterDatabase.DirectPExecute("INSERT INTO `custom_companion_phrase` (`key`, `tag`, `text`) VALUES ('%s', '%s', '%s')",
                    key.c_str(), tag.c_str(), text.c_str());
            }
        }

        void UseDefaults()
        {
            for (Line const& line : DEFAULTS)
                s_byKey[line.key].push_back({ line.tag, line.text });
        }
    }

    void Load()
    {
        s_byKey.clear();
        if (!EnsureTable())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Phrases] No table `custom_companion_phrase`: built-in phrases only.");
            UseDefaults();
            return;
        }
        Seed();
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query("SELECT `key`, `tag`, `text` FROM `custom_companion_phrase`"));
        uint32 count = 0;
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                s_byKey[f[0].GetCppString()].push_back({ f[1].GetCppString(), f[2].GetCppString() });
                ++count;
            } while (result->NextRow());
        }
        if (!count)
            UseDefaults();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [Phrases] %u companion phrases loaded.", count);
    }

    std::string Pick(std::string const& key, std::vector<std::string> const& tags)
    {
        auto itr = s_byKey.find(key);
        if (itr == s_byKey.end())
            return std::string();
        std::vector<Template const*> tagged, plain;
        for (Template const& t : itr->second)
        {
            if (t.tag == "default")
                plain.push_back(&t);
            else
                for (std::string const& tag : tags)
                    if (t.tag == tag)
                        tagged.push_back(&t);
        }
        std::vector<Template const*> const& from = tagged.empty() ? plain : tagged;
        if (from.empty())
            return std::string();
        return from[urand(0, uint32(from.size()) - 1)]->text;
    }

    std::string Format(std::string const& text, Vars const& vars)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();)
        {
            if (text[i] == '{')
            {
                size_t const end = text.find('}', i + 1);
                if (end != std::string::npos)
                {
                    auto var = vars.find(text.substr(i + 1, end - i - 1));
                    if (var != vars.end())
                    {
                        out += var->second;
                        i = end + 1;
                        continue;
                    }
                }
            }
            out += text[i++];
        }
        return out;
    }

    std::string Say(std::string const& key, std::vector<std::string> const& tags, Vars const& vars)
    {
        return Format(Pick(key, tags), vars);
    }

    std::string Word(std::string const& key, std::string const& fallback)
    {
        std::string const word = Pick(key, {});
        return word.empty() ? fallback : word;
    }

    std::string Money(uint32 copper)
    {
        uint32 const gold = copper / 10000;
        uint32 const silver = copper / 100 % 100;
        uint32 const rest = copper % 100;
        std::string out;
        if (gold)
            out += std::to_string(gold) + Word("money.gold", "g");
        if (silver)
            out += std::to_string(silver) + Word("money.silver", "s");
        if (rest || out.empty())
            out += std::to_string(rest) + Word("money.copper", "c");
        return out;
    }
}
}
