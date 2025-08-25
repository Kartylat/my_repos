using UnityEngine;

public class PlayerController : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float laneOffset = 2.0f; // distance between lanes
    [SerializeField] private float moveSpeed = 10.0f; // horizontal move lerp speed
    [SerializeField] private int currentLaneIndex = 1; // 0-left, 1-middle, 2-right

    [Header("Swipe")]
    [SerializeField] private float minSwipeDistance = 50f; // in pixels

    [Header("Attack")]
    [SerializeField] private float attackDuration = 0.2f;
    [SerializeField] private GameObject attackHitbox; // child object with collider marked as Trigger

    private Vector2 touchStartPos;
    private bool isAttacking = false;

    private void Start()
    {
        // Ensure attack hitbox is disabled initially
        if (attackHitbox != null)
        {
            attackHitbox.SetActive(false);
        }
    }

    private void Update()
    {
        HandleSwipeInput();
        MoveToTargetLane();
    }

    private void HandleSwipeInput()
    {
        if (Input.touchCount > 0)
        {
            Touch touch = Input.GetTouch(0);
            if (touch.phase == TouchPhase.Began)
            {
                touchStartPos = touch.position;
            }
            else if (touch.phase == TouchPhase.Ended || touch.phase == TouchPhase.Canceled)
            {
                Vector2 delta = touch.position - touchStartPos;
                if (delta.magnitude < minSwipeDistance) return;

                bool isHorizontal = Mathf.Abs(delta.x) > Mathf.Abs(delta.y);
                if (isHorizontal)
                {
                    if (delta.x > 0)
                    {
                        MoveRight();
                    }
                    else
                    {
                        MoveLeft();
                    }
                }
                else
                {
                    if (delta.y > 0)
                    {
                        TriggerAttack();
                    }
                }
            }
        }

#if UNITY_EDITOR || UNITY_STANDALONE
        // Editor/desktop input for testing
        if (Input.GetKeyDown(KeyCode.LeftArrow)) MoveLeft();
        if (Input.GetKeyDown(KeyCode.RightArrow)) MoveRight();
        if (Input.GetKeyDown(KeyCode.UpArrow)) TriggerAttack();
#endif
    }

    private void MoveLeft()
    {
        currentLaneIndex = Mathf.Clamp(currentLaneIndex - 1, 0, 2);
    }

    private void MoveRight()
    {
        currentLaneIndex = Mathf.Clamp(currentLaneIndex + 1, 0, 2);
    }

    private void MoveToTargetLane()
    {
        float targetX = (currentLaneIndex - 1) * laneOffset; // middle is 0
        Vector3 targetPos = new Vector3(targetX, transform.position.y, transform.position.z);
        transform.position = Vector3.Lerp(transform.position, targetPos, Time.deltaTime * moveSpeed);
    }

    private void TriggerAttack()
    {
        if (isAttacking || attackHitbox == null) return;
        isAttacking = true;
        attackHitbox.SetActive(true);
        Invoke(nameof(EndAttack), attackDuration);
    }

    private void EndAttack()
    {
        if (attackHitbox != null)
        {
            attackHitbox.SetActive(false);
        }
        isAttacking = false;
    }

    private void OnTriggerEnter(Collider other)
    {
        // If something collides with the player directly (e.g., scooter), trigger game over
        if (other.CompareTag("Scooter"))
        {
            ScoreManager.Instance?.TriggerGameOver();
        }
    }
}

